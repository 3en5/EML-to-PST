// WLM2PST - fallback EML import engine built on Windows' own MimeOle parser
// (inetcomm.dll), the same MIME engine Windows Live Mail used to write the
// source files in the first place.
//
// Why this exists: on current Click-to-Run Office, Outlook's
// IConverterSession is not usable out-of-process (field-verified across
// acquisition paths, flags, address book and AppV bootstrapping - see
// docs/verification/). MimeOle is a Windows component, present and
// registered on every supported Windows, with full charset / RFC 2047 /
// RFC 2231 handling. Parsing is done by Microsoft's engine; WLM2PST only
// maps the parsed parts onto MAPI properties - this is NOT a hand-built
// MIME parser (spec sections 3 "prefer official Microsoft code" and 33.16
// compatibility clause). IConverterSession remains the primary engine
// wherever it actually works; the preflight calibration decides.
#pragma once

#include "common/errors/result.h"
#include "worker/mapi/mapi_raii.h"
#include "worker/mapi/mapi_runtime.h"

#include <string>

namespace wlm2pst::mapi {

class MimeOleImporter {
public:
    MimeOleImporter() = default;
    MimeOleImporter(const MimeOleImporter&) = delete;
    MimeOleImporter& operator=(const MimeOleImporter&) = delete;
    MimeOleImporter(MimeOleImporter&&) = default;
    MimeOleImporter& operator=(MimeOleImporter&&) = default;

    // Loads inetcomm.dll and resolves MimeOleCreateMessage. No COM
    // registration dependency at all.
    static Result<MimeOleImporter> create();

    // Parses the EML stream with MimeOle and writes the message content
    // (subject, sender, recipients, text/HTML bodies, attachments incl.
    // inline CID parts, internet ids, dates, transport headers) onto `msg`.
    // Read/sent/draft flags, date fallbacks and tracking properties are
    // applied by the caller exactly as with the IConverterSession path.
    Status import(IStream& eml, IMessage& msg, MapiRuntime& runtime);

private:
    HMODULE inetcomm_ = nullptr;  // leaked intentionally: process-lifetime engine
    void* create_message_fn_ = nullptr;
};

}  // namespace wlm2pst::mapi
