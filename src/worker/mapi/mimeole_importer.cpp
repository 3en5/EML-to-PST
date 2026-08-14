#include "worker/mapi/mimeole_importer.h"

#include "common/logging/logger.h"
#include "common/unicode/utf.h"
#include "worker/mapi/mapi_compat.h"
#include "worker/mapi/mimeole_compat.h"

#include <propvarutil.h>

#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace wlm2pst::mapi {

namespace {

// MAPI internet-property tags used by this importer; not guaranteed present
// in every mapitags.h variant.
#ifndef PR_INTERNET_MESSAGE_ID_W
#define PR_INTERNET_MESSAGE_ID_W PROP_TAG(PT_UNICODE, 0x1035)
#endif
#ifndef PR_IN_REPLY_TO_ID_W
#define PR_IN_REPLY_TO_ID_W PROP_TAG(PT_UNICODE, 0x1042)
#endif
#ifndef PR_INTERNET_REFERENCES_W
#define PR_INTERNET_REFERENCES_W PROP_TAG(PT_UNICODE, 0x1039)
#endif
#ifndef PR_TRANSPORT_MESSAGE_HEADERS_W
#define PR_TRANSPORT_MESSAGE_HEADERS_W PROP_TAG(PT_UNICODE, 0x007D)
#endif
#ifndef PR_HTML
#define PR_HTML PROP_TAG(PT_BINARY, 0x1013)
#endif
#ifndef PR_INTERNET_CPID
#define PR_INTERNET_CPID PROP_TAG(PT_LONG, 0x3FDE)
#endif
#ifndef PR_SENDER_NAME_W
#define PR_SENDER_NAME_W PROP_TAG(PT_UNICODE, 0x0C1A)
#endif
#ifndef PR_SENDER_EMAIL_ADDRESS_W
#define PR_SENDER_EMAIL_ADDRESS_W PROP_TAG(PT_UNICODE, 0x0C1F)
#endif
#ifndef PR_SENDER_ADDRTYPE_W
#define PR_SENDER_ADDRTYPE_W PROP_TAG(PT_UNICODE, 0x0C1E)
#endif
#ifndef PR_SENT_REPRESENTING_NAME_W
#define PR_SENT_REPRESENTING_NAME_W PROP_TAG(PT_UNICODE, 0x0042)
#endif
#ifndef PR_SENT_REPRESENTING_EMAIL_ADDRESS_W
#define PR_SENT_REPRESENTING_EMAIL_ADDRESS_W PROP_TAG(PT_UNICODE, 0x0065)
#endif
#ifndef PR_SENT_REPRESENTING_ADDRTYPE_W
#define PR_SENT_REPRESENTING_ADDRTYPE_W PROP_TAG(PT_UNICODE, 0x0064)
#endif
#ifndef PR_EMAIL_ADDRESS_W
#define PR_EMAIL_ADDRESS_W PROP_TAG(PT_UNICODE, 0x3003)
#endif
#ifndef PR_ADDRTYPE_W
#define PR_ADDRTYPE_W PROP_TAG(PT_UNICODE, 0x3002)
#endif
#ifndef PR_SMTP_ADDRESS_W
#define PR_SMTP_ADDRESS_W PROP_TAG(PT_UNICODE, 0x39FE)
#endif
#ifndef PR_ATTACH_CONTENT_ID_W
#define PR_ATTACH_CONTENT_ID_W PROP_TAG(PT_UNICODE, 0x3712)
#endif
#ifndef PR_RECIPIENT_TYPE
#define PR_RECIPIENT_TYPE PROP_TAG(PT_LONG, 0x0C15)
#endif

using MimeOleCreateMessageFn = HRESULT(STDAPICALLTYPE*)(IUnknown*, IMimeMessage**);

// Reads a MimeOle property as a decoded wide string (MimeOle performs the
// RFC 2047 decode and charset conversion when VT_LPWSTR is requested).
std::optional<std::wstring> get_prop_w(IMimeMessage& mime, const char* name) {
    PROPVARIANT pv;
    PropVariantInit(&pv);
    pv.vt = VT_LPWSTR;
    if (FAILED(mime.GetProp(name, 0, &pv))) return std::nullopt;
    std::optional<std::wstring> result;
    if (pv.vt == VT_LPWSTR && pv.pwszVal) result = pv.pwszVal;
    PropVariantClear(&pv);
    return result;
}

std::optional<std::wstring> get_body_prop_w(IMimePropertySet& props, const char* name) {
    PROPVARIANT pv;
    PropVariantInit(&pv);
    pv.vt = VT_LPWSTR;
    if (FAILED(props.GetProp(name, 0, &pv))) return std::nullopt;
    std::optional<std::wstring> result;
    if (pv.vt == VT_LPWSTR && pv.pwszVal) result = pv.pwszVal;
    PropVariantClear(&pv);
    return result;
}

// Drains an IStream fully into a byte vector (bounded by caller knowledge:
// bodies/attachments of one message).
Result<std::vector<uint8_t>> drain_stream(IStream& stream) {
    std::vector<uint8_t> out;
    LARGE_INTEGER zero{};
    (void)stream.Seek(zero, STREAM_SEEK_SET, nullptr);
    uint8_t buf[64 * 1024];
    for (;;) {
        ULONG read = 0;
        HRESULT hr = stream.Read(buf, sizeof(buf), &read);
        if (FAILED(hr)) return make_hresult_error(static_cast<int32_t>(hr), "IStream::Read");
        if (read == 0) break;
        out.insert(out.end(), buf, buf + read);
    }
    return out;
}

// UTF-16 stream (IET_UNICODE) -> wstring, tolerant of a BOM.
Result<std::wstring> drain_utf16_stream(IStream& stream) {
    auto bytes = drain_stream(stream);
    if (!bytes.ok()) return bytes.error();
    const auto& b = bytes.value();
    size_t offset = 0;
    if (b.size() >= 2 && b[0] == 0xFF && b[1] == 0xFE) offset = 2;
    std::wstring out;
    out.reserve((b.size() - offset) / 2);
    for (size_t i = offset; i + 1 < b.size(); i += 2) {
        out.push_back(static_cast<wchar_t>(b[i] | (b[i + 1] << 8)));
    }
    return out;
}

Status set_unicode_prop(IMessage& msg, ULONG tag, const std::wstring& value) {
    SPropValue prop{};
    prop.ulPropTag = tag;
    prop.Value.lpszW = const_cast<LPWSTR>(value.c_str());
    HRESULT hr = msg.SetProps(1, &prop, nullptr);
    if (FAILED(hr)) return make_hresult_error(static_cast<int32_t>(hr), "IMessage::SetProps");
    return Status::success();
}

// Writes a large/binary property through a stream.
Status write_stream_prop(IMessage& msg, ULONG tag, const void* data, size_t size,
                         const char* what) {
    MapiPtr<IStream> stream;
    HRESULT hr = msg.OpenProperty(tag, &IID_IStream, 0, MAPI_CREATE | MAPI_MODIFY,
                                  stream.put_unknown());
    if (FAILED(hr)) {
        return make_hresult_error(static_cast<int32_t>(hr),
                                  std::string("OpenProperty(") + what + ")");
    }
    const uint8_t* p = static_cast<const uint8_t*>(data);
    size_t remaining = size;
    while (remaining > 0) {
        ULONG chunk = static_cast<ULONG>(remaining > 256 * 1024 ? 256 * 1024 : remaining);
        ULONG written = 0;
        hr = stream->Write(p, chunk, &written);
        if (FAILED(hr) || written == 0) {
            return make_hresult_error(static_cast<int32_t>(hr),
                                      std::string("IStream::Write(") + what + ")");
        }
        p += written;
        remaining -= written;
    }
    hr = stream->Commit(STGC_DEFAULT);
    if (FAILED(hr)) return make_hresult_error(static_cast<int32_t>(hr), "IStream::Commit");
    return Status::success();
}

std::wstring ansi_to_wide(const char* s) {
    if (!s || !*s) return {};
    int needed = MultiByteToWideChar(CP_ACP, 0, s, -1, nullptr, 0);
    if (needed <= 1) return {};
    std::wstring out(static_cast<size_t>(needed - 1), L'\0');
    MultiByteToWideChar(CP_ACP, 0, s, -1, out.data(), needed);
    return out;
}

struct ParsedAddress {
    std::wstring name;
    std::wstring email;
};

// Collects one address class (IAT_*) into name/email pairs. MimeOle owns the
// returned ADDRESSLIST memory via the task allocator.
std::vector<ParsedAddress> collect_addresses(IMimeMessage& mime, DWORD address_type) {
    std::vector<ParsedAddress> out;
    ADDRESSLIST list{};
    if (FAILED(mime.GetAddressTypes(address_type, IAP_EMAIL | IAP_FRIENDLY | IAP_ADRTYPE,
                                    &list))) {
        return out;
    }
    for (ULONG i = 0; i < list.cAdrs; ++i) {
        ADDRESSPROPS& a = list.prgAdr[i];
        ParsedAddress parsed;
        parsed.email = ansi_to_wide(a.pszEmail);  // RFC 5321 addresses are ASCII
        parsed.name = ansi_to_wide(a.pszFriendly);
        if (parsed.name.empty()) parsed.name = parsed.email;
        if (!parsed.email.empty() || !parsed.name.empty()) out.push_back(std::move(parsed));
        if (a.pszEmail) CoTaskMemFree(a.pszEmail);
        if (a.pszFriendly) CoTaskMemFree(a.pszFriendly);
    }
    if (list.prgAdr) CoTaskMemFree(list.prgAdr);
    return out;
}

// Builds and applies the MAPI recipient table.
Status add_recipients(IMessage& msg, MapiRuntime& runtime,
                      const std::vector<std::pair<LONG, ParsedAddress>>& recipients) {
    if (recipients.empty()) return Status::success();

    // One ADRLIST allocation; per-entry SPropValue arrays chained with
    // MAPIAllocateMore so a single MAPIFreeBuffer releases everything.
    ULONG bytes = CbNewADRLIST(static_cast<ULONG>(recipients.size()));
    LPADRLIST adrlist = nullptr;
    if (runtime.MAPIAllocateBuffer(bytes, reinterpret_cast<LPVOID*>(&adrlist)) != S_OK ||
        !adrlist) {
        return make_error("MAPIAllocateBuffer", "recipient list allocation failed");
    }
    MapiBuffer guard(runtime.MAPIFreeBuffer);
    *guard.put() = adrlist;
    adrlist->cEntries = static_cast<ULONG>(recipients.size());

    static wchar_t kSmtp[] = L"SMTP";
    for (size_t i = 0; i < recipients.size(); ++i) {
        const auto& [recipient_type, address] = recipients[i];
        constexpr ULONG kProps = 5;
        LPSPropValue values = nullptr;
        if (runtime.MAPIAllocateMore(kProps * sizeof(SPropValue), adrlist,
                                     reinterpret_cast<LPVOID*>(&values)) != S_OK) {
            return make_error("MAPIAllocateMore", "recipient entry allocation failed");
        }
        auto copy_wide = [&](const std::wstring& s) -> LPWSTR {
            LPWSTR dest = nullptr;
            ULONG cb = static_cast<ULONG>((s.size() + 1) * sizeof(wchar_t));
            if (runtime.MAPIAllocateMore(cb, adrlist, reinterpret_cast<LPVOID*>(&dest)) != S_OK) {
                return nullptr;
            }
            std::memcpy(dest, s.c_str(), cb);
            return dest;
        };
        LPWSTR name = copy_wide(address.name.empty() ? address.email : address.name);
        LPWSTR email = copy_wide(address.email);
        if (!name || !email) return make_error("MAPIAllocateMore", "recipient string allocation");

        values[0].ulPropTag = PR_RECIPIENT_TYPE;
        values[0].Value.l = recipient_type;
        values[1].ulPropTag = PR_DISPLAY_NAME_W;
        values[1].Value.lpszW = name;
        values[2].ulPropTag = PR_EMAIL_ADDRESS_W;
        values[2].Value.lpszW = email;
        values[3].ulPropTag = PR_ADDRTYPE_W;
        values[3].Value.lpszW = kSmtp;
        values[4].ulPropTag = PR_SMTP_ADDRESS_W;
        values[4].Value.lpszW = email;

        adrlist->aEntries[i].ulReserved1 = 0;
        adrlist->aEntries[i].cValues = kProps;
        adrlist->aEntries[i].rgPropVals = values;
    }

    HRESULT hr = msg.ModifyRecipients(MODRECIP_ADD, adrlist);
    if (FAILED(hr)) {
        return make_hresult_error(static_cast<int32_t>(hr), "IMessage::ModifyRecipients");
    }
    return Status::success();
}

Status add_attachment(IMessage& msg, IMimeMessage& mime, HBODY hbody) {
    MapiPtr<IMimeBody> body;
    HRESULT hr = mime.BindToObject(hbody, kIidIMimeBody, body.put_void());
    if (FAILED(hr)) {
        return make_hresult_error(static_cast<int32_t>(hr), "IMimeMessageTree::BindToObject");
    }

    // Decoded attachment bytes (MimeOle handles base64/QP/uuencode).
    MapiPtr<IStream> data;
    hr = body->GetData(IET_BINARY, data.put());
    if (FAILED(hr)) {
        return make_hresult_error(static_cast<int32_t>(hr), "IMimeBody::GetData");
    }
    auto bytes = drain_stream(*data.get());
    if (!bytes.ok()) return bytes.error();

    // Filename: MimeOle's computed generated-filename property handles
    // RFC 2231/2047 encoded names and invents one when the source has none.
    std::wstring filename = L"attachment";
    if (auto gen = get_body_prop_w(*body.get(), "att:generated-filename")) {
        filename = *gen;
    } else if (auto fn = get_body_prop_w(*body.get(), "att:filename")) {
        filename = *fn;
    }
    std::optional<std::wstring> pri = get_body_prop_w(*body.get(), "att:pritype");
    std::optional<std::wstring> sub = get_body_prop_w(*body.get(), "att:subtype");
    std::optional<std::wstring> content_id = get_body_prop_w(*body.get(), "content-id");

    ULONG attach_num = 0;
    MapiPtr<IAttach> attach;
    hr = msg.CreateAttach(nullptr, 0, &attach_num, attach.put());
    if (FAILED(hr)) return make_hresult_error(static_cast<int32_t>(hr), "IMessage::CreateAttach");

    std::wstring mime_tag;
    if (pri && sub) mime_tag = *pri + L"/" + *sub;

    std::vector<SPropValue> props;
    props.reserve(6);
    SPropValue p{};
    p.ulPropTag = PR_ATTACH_METHOD;
    p.Value.ul = ATTACH_BY_VALUE;
    props.push_back(p);
    p = {};
    p.ulPropTag = PR_ATTACH_LONG_FILENAME_W;
    p.Value.lpszW = const_cast<LPWSTR>(filename.c_str());
    props.push_back(p);
    p = {};
    p.ulPropTag = PR_ATTACH_FILENAME_W;
    p.Value.lpszW = const_cast<LPWSTR>(filename.c_str());
    props.push_back(p);
    p = {};
    p.ulPropTag = PR_DISPLAY_NAME_W;
    p.Value.lpszW = const_cast<LPWSTR>(filename.c_str());
    props.push_back(p);
    if (!mime_tag.empty()) {
        p = {};
        p.ulPropTag = PR_ATTACH_MIME_TAG_W;
        p.Value.lpszW = const_cast<LPWSTR>(mime_tag.c_str());
        props.push_back(p);
    }
    std::wstring cid;
    if (content_id) {
        cid = *content_id;
        // Strip surrounding <> per convention.
        if (cid.size() >= 2 && cid.front() == L'<' && cid.back() == L'>') {
            cid = cid.substr(1, cid.size() - 2);
        }
        if (!cid.empty()) {
            p = {};
            p.ulPropTag = PR_ATTACH_CONTENT_ID_W;
            p.Value.lpszW = const_cast<LPWSTR>(cid.c_str());
            props.push_back(p);
        }
    }
    hr = attach->SetProps(static_cast<ULONG>(props.size()), props.data(), nullptr);
    if (FAILED(hr)) return make_hresult_error(static_cast<int32_t>(hr), "IAttach::SetProps");

    // Attachment bytes.
    MapiPtr<IStream> dest;
    hr = attach->OpenProperty(PR_ATTACH_DATA_BIN, &IID_IStream, 0, MAPI_CREATE | MAPI_MODIFY,
                              dest.put_unknown());
    if (FAILED(hr)) {
        return make_hresult_error(static_cast<int32_t>(hr),
                                  "IAttach::OpenProperty(PR_ATTACH_DATA_BIN)");
    }
    const auto& blob = bytes.value();
    size_t offset = 0;
    while (offset < blob.size()) {
        ULONG chunk = static_cast<ULONG>(
            blob.size() - offset > 256 * 1024 ? 256 * 1024 : blob.size() - offset);
        ULONG written = 0;
        hr = dest->Write(blob.data() + offset, chunk, &written);
        if (FAILED(hr) || written == 0) {
            return make_hresult_error(static_cast<int32_t>(hr), "IStream::Write(attach)");
        }
        offset += written;
    }
    if (blob.empty()) {
        // Zero-byte attachments are legal; stream stays empty.
    }
    hr = dest->Commit(STGC_DEFAULT);
    if (FAILED(hr)) return make_hresult_error(static_cast<int32_t>(hr), "IStream::Commit(attach)");
    hr = attach->SaveChanges(0);
    if (FAILED(hr)) return make_hresult_error(static_cast<int32_t>(hr), "IAttach::SaveChanges");
    return Status::success();
}

}  // namespace

Result<MimeOleImporter> MimeOleImporter::create() {
    MimeOleImporter importer;
    importer.inetcomm_ = LoadLibraryExW(L"inetcomm.dll", nullptr,
                                        LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!importer.inetcomm_) {
        return make_win32_error(GetLastError(), "LoadLibraryExW(inetcomm.dll)",
                                "Windows MimeOle engine unavailable");
    }
    importer.create_message_fn_ = reinterpret_cast<void*>(
        GetProcAddress(importer.inetcomm_, "MimeOleCreateMessage"));
    if (!importer.create_message_fn_) {
        return make_error("GetProcAddress(MimeOleCreateMessage)",
                          "inetcomm.dll is present but does not export the MimeOle factory");
    }
    return importer;
}

Status MimeOleImporter::import(IStream& eml, IMessage& msg, MapiRuntime& runtime) {
    auto create_message = reinterpret_cast<MimeOleCreateMessageFn>(create_message_fn_);

    MapiPtr<IMimeMessage> mime;
    HRESULT hr = create_message(nullptr, mime.put());
    if (FAILED(hr)) {
        return make_hresult_error(static_cast<int32_t>(hr), "MimeOleCreateMessage");
    }
    // Preserve the raw internet transport headers (spec section 10) before
    // handing the stream to the parser: capture up to the header/body
    // separator from the original bytes (ASCII + encoded-words; widened 1:1).
    std::wstring transport_headers;
    {
        LARGE_INTEGER zero{};
        (void)eml.Seek(zero, STREAM_SEEK_SET, nullptr);
        std::vector<char> prefix(64 * 1024);
        ULONG read = 0;
        if (SUCCEEDED(eml.Read(prefix.data(), static_cast<ULONG>(prefix.size()), &read))) {
            std::string_view view(prefix.data(), read);
            size_t end = view.find("\r\n\r\n");
            size_t skip = 4;
            if (end == std::string_view::npos) { end = view.find("\n\n"); skip = 2; }
            if (end != std::string_view::npos) {
                transport_headers.reserve(end + skip);
                for (size_t i = 0; i < end + skip; ++i) {
                    transport_headers.push_back(
                        static_cast<wchar_t>(static_cast<unsigned char>(view[i])));
                }
            }
        }
    }

    LARGE_INTEGER zero{};
    (void)eml.Seek(zero, STREAM_SEEK_SET, nullptr);
    hr = mime->Load(&eml);
    if (FAILED(hr)) {
        return make_hresult_error(static_cast<int32_t>(hr), "IMimeMessage::Load");
    }
    if (!transport_headers.empty()) {
        (void)set_unicode_prop(msg, PR_TRANSPORT_MESSAGE_HEADERS_W, transport_headers);
    }

    // Message class first: everything imported through this engine is a note.
    if (Status s = set_unicode_prop(msg, PR_MESSAGE_CLASS_W, L"IPM.Note"); !s.ok()) return s;

    // Subject (decoded from RFC 2047 by MimeOle).
    if (auto subject = get_prop_w(*mime.get(), "Subject")) {
        if (Status s = set_unicode_prop(msg, PR_SUBJECT_W, *subject); !s.ok()) return s;
    }

    // Sender.
    auto from = collect_addresses(*mime.get(), IAT_FROM);
    if (from.empty()) from = collect_addresses(*mime.get(), IAT_SENDER);
    if (!from.empty()) {
        const ParsedAddress& sender = from.front();
        static wchar_t kSmtp[] = L"SMTP";
        SPropValue props[6]{};
        props[0].ulPropTag = PR_SENDER_NAME_W;
        props[0].Value.lpszW = const_cast<LPWSTR>(sender.name.c_str());
        props[1].ulPropTag = PR_SENDER_EMAIL_ADDRESS_W;
        props[1].Value.lpszW = const_cast<LPWSTR>(sender.email.c_str());
        props[2].ulPropTag = PR_SENDER_ADDRTYPE_W;
        props[2].Value.lpszW = kSmtp;
        props[3].ulPropTag = PR_SENT_REPRESENTING_NAME_W;
        props[3].Value.lpszW = const_cast<LPWSTR>(sender.name.c_str());
        props[4].ulPropTag = PR_SENT_REPRESENTING_EMAIL_ADDRESS_W;
        props[4].Value.lpszW = const_cast<LPWSTR>(sender.email.c_str());
        props[5].ulPropTag = PR_SENT_REPRESENTING_ADDRTYPE_W;
        props[5].Value.lpszW = kSmtp;
        hr = msg.SetProps(6, props, nullptr);
        if (FAILED(hr)) {
            return make_hresult_error(static_cast<int32_t>(hr), "IMessage::SetProps(sender)");
        }
    }

    // Recipients (To/Cc/Bcc - BCC only exists when the source EML carried it).
    std::vector<std::pair<LONG, ParsedAddress>> recipients;
    for (auto& a : collect_addresses(*mime.get(), IAT_TO)) recipients.emplace_back(MAPI_TO, a);
    for (auto& a : collect_addresses(*mime.get(), IAT_CC)) recipients.emplace_back(MAPI_CC, a);
    for (auto& a : collect_addresses(*mime.get(), IAT_BCC)) recipients.emplace_back(MAPI_BCC, a);
    if (Status s = add_recipients(msg, runtime, recipients); !s.ok()) return s;

    // Bodies. IET_UNICODE makes MimeOle deliver decoded UTF-16 regardless of
    // the source charset (windows-1255, ISO-8859-8, UTF-8, ...).
    bool has_any_body = false;
    {
        MapiPtr<IStream> plain;
        HBODY hbody = nullptr;
        if (SUCCEEDED(mime->GetTextBody(TXT_PLAIN, IET_UNICODE, plain.put(), &hbody))) {
            auto text = drain_utf16_stream(*plain.get());
            if (!text.ok()) return text.error();
            if (Status s = write_stream_prop(msg, PR_BODY_W, text.value().c_str(),
                                             (text.value().size() + 1) * sizeof(wchar_t),
                                             "PR_BODY_W");
                !s.ok()) {
                return s;
            }
            has_any_body = true;
        }
    }
    {
        MapiPtr<IStream> html;
        HBODY hbody = nullptr;
        if (SUCCEEDED(mime->GetTextBody(TXT_HTML, IET_UNICODE, html.put(), &hbody))) {
            auto text = drain_utf16_stream(*html.get());
            if (!text.ok()) return text.error();
            std::string utf8 = utf8_from_wide(text.value());
            if (Status s = write_stream_prop(msg, PR_HTML, utf8.data(), utf8.size(), "PR_HTML");
                !s.ok()) {
                return s;
            }
            SPropValue cpid{};
            cpid.ulPropTag = PR_INTERNET_CPID;
            cpid.Value.l = 65001;  // the PR_HTML bytes above are UTF-8
            (void)msg.SetProps(1, &cpid, nullptr);
            has_any_body = true;
        }
    }
    (void)has_any_body;  // empty-body messages are legal (fixture: empty_body.eml)

    // Internet identifiers (threading survives the migration).
    if (auto message_id = get_prop_w(*mime.get(), "Message-ID")) {
        (void)set_unicode_prop(msg, PR_INTERNET_MESSAGE_ID_W, *message_id);
    }
    if (auto in_reply_to = get_prop_w(*mime.get(), "In-Reply-To")) {
        (void)set_unicode_prop(msg, PR_IN_REPLY_TO_ID_W, *in_reply_to);
    }
    if (auto references = get_prop_w(*mime.get(), "References")) {
        (void)set_unicode_prop(msg, PR_INTERNET_REFERENCES_W, *references);
    }

    // Attachments, inline CID images included (GetAttachments reports every
    // non-rendered leaf; PR_ATTACH_CONTENT_ID keeps cid: references working).
    ULONG attach_count = 0;
    LPHBODY attach_handles = nullptr;
    hr = mime->GetAttachments(&attach_count, &attach_handles);
    if (SUCCEEDED(hr) && attach_handles) {
        Status result = Status::success();
        for (ULONG i = 0; i < attach_count && result.ok(); ++i) {
            result = add_attachment(msg, *mime.get(), attach_handles[i]);
        }
        CoTaskMemFree(attach_handles);
        if (!result.ok()) return result;
    }

    return Status::success();
}

}  // namespace wlm2pst::mapi
