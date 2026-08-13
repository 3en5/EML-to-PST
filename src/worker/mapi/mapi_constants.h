// WLM2PST - single home for every MAPI magic value the tool depends on
// (spec section 8: keep provider service names, GUIDs, profile properties and
// compatibility handling isolated in one well-documented module).
//
// Sources: Microsoft Extended MAPI documentation and the Microsoft MFCMAPI
// project (https://github.com/microsoft/mfcmapi), which demonstrates the
// supported way to create PST profiles and drive IConverterSession.
#pragma once

#include "worker/mapi/mapi_compat.h"

#include <objbase.h>

namespace wlm2pst::mapi {

// ---------------------------------------------------------------------------
// PST message service
// ---------------------------------------------------------------------------

// Message service name of the Unicode PST provider ("Personal Folders",
// Unicode format). The ANSI provider is "MSPST MS" and is deliberately NOT
// used (spec section 8: never create an ANSI PST).
inline constexpr const char* kUnicodePstServiceName = "MSUPST MS";

// Profile-section properties consumed by the PST provider when configuring
// the service. Not present in public SDK headers; values match MFCMAPI's
// MSPST.h usage (PR_PST_PATH_W = PROP_TAG(PT_UNICODE, 0x6700)).
#ifndef PR_PST_PATH_W
#define PR_PST_PATH_W PROP_TAG(PT_UNICODE, 0x6700)
#endif

// ---------------------------------------------------------------------------
// Profile naming
// ---------------------------------------------------------------------------

// Every profile the tool creates starts with this prefix; startup cleanup may
// delete ONLY profiles carrying it (spec section 8).
inline constexpr const wchar_t* kProfilePrefixW = L"WLM2PST-";
inline constexpr const char* kProfilePrefixA = "WLM2PST-";
inline constexpr const wchar_t* kValidateProfilePrefixW = L"WLM2PST-VALIDATE-";

// ---------------------------------------------------------------------------
// MAPI runtime discovery
// ---------------------------------------------------------------------------

// Registry location of the default MAPI client's true DLL path. Loading the
// DLL named here (typically olmapi32.dll) instead of blindly trusting
// mapi32.dll follows Microsoft's MAPI Stub Library guidance: the system stub
// can point at a non-Outlook MAPI implementation.
inline constexpr const wchar_t* kMailClientKey = L"SOFTWARE\\Clients\\Mail\\Microsoft Outlook";
inline constexpr const wchar_t* kMailClientDllPathEx = L"DllPathEx";
inline constexpr const wchar_t* kMailClientDllPath = L"DllPath";
inline constexpr const wchar_t* kMapiStubDll = L"mapi32.dll";

// ---------------------------------------------------------------------------
// IConverterSession (MIME <-> MAPI), registered by classic Outlook.
// The interface is not published in SDK headers; the vtable layout below
// matches MFCMAPI (core/interpret/guid.h + MrMAPI mime handling) and the
// documented "Importing MIME email" Microsoft sample.
// ---------------------------------------------------------------------------

// {4E3A7680-B77A-11D0-9DA5-00C04FD65685}
inline constexpr CLSID kClsidIConverterSession =
    {0x4e3a7680, 0xb77a, 0x11d0, {0x9d, 0xa5, 0x00, 0xc0, 0x4f, 0xd6, 0x56, 0x85}};
// {4B401570-B77B-11D0-9DA5-00C04FD65685}
inline constexpr IID kIidIConverterSession =
    {0x4b401570, 0xb77b, 0x11d0, {0x9d, 0xa5, 0x00, 0xc0, 0x4f, 0xd6, 0x56, 0x85}};

// IID_IMsgServiceAdmin2 (OLEGUID 0x00020387). Defined locally because SDK
// headers only declare it extern and no import library provides the symbol.
inline constexpr IID kIidIMsgServiceAdmin2 =
    {0x00020387, 0x0000, 0x0000, {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};

// CCSF_* conversion flags (Microsoft documented values).
inline constexpr ULONG kCcsfSmtp = 0x0002;            // treat as SMTP/MIME message
inline constexpr ULONG kCcsfIncludeBcc = 0x0020;      // preserve BCC when present
inline constexpr ULONG kCcsfGlobalMessage = 0x00020000;  // international/EAI headers (Outlook 2010+)

// Types referenced by the IConverterSession vtable that live in Inet headers
// we do not otherwise need; minimal stand-ins keep the layout identical.
using ConverterEncodingType = int;   // ENCODINGTYPE
using ConverterMimeSaveType = int;   // MIMESAVETYPE
using ConverterHCharset = void*;     // HCHARSET
using ConverterCsetApplyType = int;  // CSETAPPLYTYPE

// Vtable-layout-accurate declaration (do not reorder).
class IConverterSession : public IUnknown {
public:
    virtual HRESULT STDMETHODCALLTYPE SetAdrBook(LPADRBOOK pab) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetEncoding(ConverterEncodingType et) = 0;
    virtual HRESULT STDMETHODCALLTYPE PlaceHolder1() = 0;
    virtual HRESULT STDMETHODCALLTYPE MIMEToMAPI(LPSTREAM pstm, LPMESSAGE pmsg,
                                                 LPCSTR pszSrcSrv, ULONG ulFlags) = 0;
    virtual HRESULT STDMETHODCALLTYPE MAPIToMIMEStm(LPMESSAGE pmsg, LPSTREAM pstm,
                                                    ULONG ulFlags) = 0;
    virtual HRESULT STDMETHODCALLTYPE PlaceHolder2() = 0;
    virtual HRESULT STDMETHODCALLTYPE PlaceHolder3() = 0;
    virtual HRESULT STDMETHODCALLTYPE PlaceHolder4() = 0;
    virtual HRESULT STDMETHODCALLTYPE SetTextWrapping(BOOL fWrapText, ULONG ulWrapWidth) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetSaveFormat(ConverterMimeSaveType mstSaveFormat) = 0;
    virtual HRESULT STDMETHODCALLTYPE PlaceHolder5() = 0;
    virtual HRESULT STDMETHODCALLTYPE SetCharset(BOOL fApply, ConverterHCharset hcharset,
                                                 ConverterCsetApplyType csetapplytype) = 0;
};

// ---------------------------------------------------------------------------
// WLM2PST named tracking properties (spec section 15)
// ---------------------------------------------------------------------------

// Fixed property-set GUID owned by WLM2PST. Generated once for this tool and
// never changed: {D8E3C2A6-51B4-4E0D-9A67-3F2B8C1D4A90}
inline constexpr GUID kWlm2PstPropertySetGuid =
    {0xd8e3c2a6, 0x51b4, 0x4e0d, {0x9a, 0x67, 0x3f, 0x2b, 0x8c, 0x1d, 0x4a, 0x90}};

// MNID_STRING property names. Order matters: resolve_tracking_props() maps
// them positionally onto TrackingProps fields.
inline constexpr const wchar_t* kPropSourceRelativePath = L"Wlm2Pst.SourceRelativePath";  // PT_UNICODE
inline constexpr const wchar_t* kPropSourceSha256 = L"Wlm2Pst.SourceSha256";              // PT_UNICODE (lowercase hex)
inline constexpr const wchar_t* kPropImportVersion = L"Wlm2Pst.ImportVersion";            // PT_UNICODE
inline constexpr const wchar_t* kPropRunId = L"Wlm2Pst.RunId";                            // PT_UNICODE
inline constexpr const wchar_t* kPropSourceSize = L"Wlm2Pst.SourceSize";                  // PT_I8
inline constexpr const wchar_t* kPropSourceLastWriteUtc = L"Wlm2Pst.SourceLastWriteUtc";  // PT_SYSTIME

// ---------------------------------------------------------------------------
// Message flags / classes used by the import path
// ---------------------------------------------------------------------------

inline constexpr const wchar_t* kFallbackMessageClass = L"IPM.Note";
// The "_Conversion Errors" folder name lives in common/model.h
// (kConversionErrorsFolderName) so the portable pipeline shares it.

// PR_MESSAGE_FLAGS bits (mapidefs.h provides MSGFLAG_* on both toolchains,
// listed here for the reader): MSGFLAG_READ 0x1, MSGFLAG_UNSENT 0x8,
// MSGFLAG_FROMME 0x20.

}  // namespace wlm2pst::mapi
