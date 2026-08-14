// WLM2PST - MimeOle (Windows inetcomm.dll) header access.
//
// The production MSVC build uses the real <mimeole.h> from the Windows SDK -
// that declaration is authoritative. MinGW compile-check builds do not ship
// mimeole.h, so a minimal, vtable-order-accurate subset is declared here for
// them, transcribed from the published interface definitions (verified
// against the WINE project's mimeole.idl, which documents the identical
// binary interface). Only the members WLM2PST actually uses are exercised;
// every method is declared so vtable offsets stay correct.
#pragma once

#include "common/win_compat.h"

#if __has_include(<mimeole.h>)

#include <mimeole.h>
#define WLM2PST_HAVE_REAL_MIMEOLE 1

#else  // MinGW compile-check fallback declarations

#include <objidl.h>
#include <ocidl.h>
#include <propidl.h>

#define WLM2PST_HAVE_REAL_MIMEOLE 0

typedef const PROPVARIANT* LPCPROPVARIANT;

DECLARE_HANDLE(HCHARSET);
DECLARE_HANDLE(HBODY);
DECLARE_HANDLE(HADDRESS);
typedef HBODY* LPHBODY;
typedef DWORD TYPEDID;

typedef enum tagENCODINGTYPE {
    IET_BINARY, IET_BASE64, IET_UUENCODE, IET_QP, IET_7BIT, IET_8BIT,
    IET_INETCSET, IET_UNICODE, IET_RFC1522, IET_ENCODED, IET_CURRENT,
    IET_UNKNOWN, IET_BINHEX40, IET_LAST
} ENCODINGTYPE;

typedef enum tagCSETAPPLYTYPE {
    CSET_APPLY_UNTAGGED, CSET_APPLY_ALL, CSET_APPLY_TAG_ALL
} CSETAPPLYTYPE;

typedef enum tagIMSGBODYTYPE {
    IBT_SECURE, IBT_ATTACHMENT, IBT_EMPTY, IBT_CSETTAGGED, IBT_AUTOATTACH
} IMSGBODYTYPE;

typedef enum tagBODYLOCATION {
    IBL_ROOT, IBL_PARENT, IBL_FIRST, IBL_LAST, IBL_NEXT, IBL_PREVIOUS
} BODYLOCATION;

typedef struct tagFINDBODY { LPSTR pszPriType; LPSTR pszSubType; DWORD dwReserved; } FINDBODY, *LPFINDBODY;
typedef struct tagBODYOFFSETS { DWORD cbBoundaryStart; DWORD cbHeaderStart; DWORD cbBodyStart; DWORD cbBodyEnd; } BODYOFFSETS, *LPBODYOFFSETS;
typedef struct tagTRANSMITINFO {
    ENCODINGTYPE ietCurrent; ENCODINGTYPE ietXmitMime; ENCODINGTYPE ietXmit822;
    ULONG cbLongestLine; ULONG cExtended; ULONG ulPercentExt; ULONG cbSize; ULONG cLines;
} TRANSMITINFO, *LPTRANSMITINFO;
typedef struct tagMIMEPARAMINFO { LPSTR pszName; LPSTR pszData; } MIMEPARAMINFO, *LPMIMEPARAMINFO;
typedef struct tagMIMEPROPINFO {
    DWORD dwMask; HCHARSET hCharset; ENCODINGTYPE ietEncoding; DWORD dwRowNumber;
    DWORD dwFlags; DWORD dwPropId; DWORD cValues; VARTYPE vtDefault; VARTYPE vtCurrent;
} MIMEPROPINFO, *LPMIMEPROPINFO;
typedef const MIMEPROPINFO* LPCMIMEPROPINFO;

typedef enum tagCERTSTATE {
    CERTIFICATE_OK, CERTIFICATE_NOT_PRESENT, CERTIFICATE_EXPIRED,
    CERTIFICATE_CHAIN_TOO_LONG, CERTIFICATE_MISSING_ISSUER, CERTIFICATE_CRL_LISTED,
    CERTIFICATE_NOT_TRUSTED, CERTIFICATE_INVALID, CERTIFICATE_ERROR,
    CERTIFICATE_NOPRINT, CERTIFICATE_UNKNOWN
} CERTSTATE;
typedef BLOB THUMBBLOB;

typedef struct tagADDRESSPROPS {
    DWORD dwProps; HADDRESS hAddress; ENCODINGTYPE ietFriendly; HCHARSET hCharset;
    DWORD dwAdrType; LPSTR pszFriendly; LPWSTR pwszReserved; LPSTR pszEmail;
    CERTSTATE certstate; THUMBBLOB tbSigning; THUMBBLOB tbEncryption;
    DWORD dwCookie; DWORD dwReserved1; DWORD dwReserved2;
} ADDRESSPROPS, *LPADDRESSPROPS;

typedef struct tagADDRESSLIST { ULONG cAdrs; LPADDRESSPROPS prgAdr; } ADDRESSLIST, *LPADDRESSLIST;

typedef enum tagADDRESSFORMAT {
    AFT_DISPLAY_FRIENDLY, AFT_DISPLAY_EMAIL, AFT_DISPLAY_BOTH,
    AFT_RFC822_DECODED, AFT_RFC822_ENCODED, AFT_RFC822_TRANSMIT
} ADDRESSFORMAT;

#define IAT_FROM 0x00000001
#define IAT_SENDER 0x00000002
#define IAT_TO 0x00000004
#define IAT_CC 0x00000008
#define IAT_BCC 0x00000010
#define IAP_ADRTYPE 0x00000004
#define IAP_FRIENDLY 0x00000008
#define IAP_EMAIL 0x00000020

#define TXT_PLAIN 1
#define TXT_HTML 2
#define HBODY_ROOT ((HBODY)-1)

struct IMimeEnumProperties;
struct IMimeEnumAddressTypes;
struct IMimeMessageParts;
struct IMimeMessageCallback;
struct IMimePropertySet;
struct IMimeAddressTable;

// Vtable-order-accurate: IMimePropertySet : IPersistStreamInit.
struct IMimePropertySet : public IPersistStreamInit {
    virtual HRESULT STDMETHODCALLTYPE GetPropInfo(LPCSTR, LPMIMEPROPINFO) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetPropInfo(LPCSTR, LPCMIMEPROPINFO) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetProp(LPCSTR, DWORD, LPPROPVARIANT) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetProp(LPCSTR, DWORD, LPCPROPVARIANT) = 0;
    virtual HRESULT STDMETHODCALLTYPE AppendProp(LPCSTR, DWORD, LPPROPVARIANT) = 0;
    virtual HRESULT STDMETHODCALLTYPE DeleteProp(LPCSTR) = 0;
    virtual HRESULT STDMETHODCALLTYPE CopyProps(ULONG, LPCSTR*, IMimePropertySet*) = 0;
    virtual HRESULT STDMETHODCALLTYPE MoveProps(ULONG, LPCSTR*, IMimePropertySet*) = 0;
    virtual HRESULT STDMETHODCALLTYPE DeleteExcept(ULONG, LPCSTR*) = 0;
    virtual HRESULT STDMETHODCALLTYPE QueryProp(LPCSTR, LPCSTR, BOOL, BOOL) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetCharset(HCHARSET*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetCharset(HCHARSET, CSETAPPLYTYPE) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetParameters(LPCSTR, ULONG*, LPMIMEPARAMINFO*) = 0;
    virtual HRESULT STDMETHODCALLTYPE IsContentType(LPCSTR, LPCSTR) = 0;
    virtual HRESULT STDMETHODCALLTYPE BindToObject(REFIID, void**) = 0;
    virtual HRESULT STDMETHODCALLTYPE Clone(IMimePropertySet**) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetOption(const TYPEDID, LPCPROPVARIANT) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetOption(const TYPEDID, LPPROPVARIANT) = 0;
    virtual HRESULT STDMETHODCALLTYPE EnumProps(DWORD, IMimeEnumProperties**) = 0;
};

struct IMimeBody : public IMimePropertySet {
    virtual HRESULT STDMETHODCALLTYPE IsType(IMSGBODYTYPE) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDisplayName(LPCSTR) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDisplayName(LPSTR*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetOffsets(LPBODYOFFSETS) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetCurrentEncoding(ENCODINGTYPE*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetCurrentEncoding(ENCODINGTYPE) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetEstimatedSize(ENCODINGTYPE, ULONG*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDataHere(ENCODINGTYPE, IStream*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetData(ENCODINGTYPE, IStream**) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetData(ENCODINGTYPE, LPCSTR, LPCSTR, REFIID, LPVOID) = 0;
    virtual HRESULT STDMETHODCALLTYPE EmptyData() = 0;
    virtual HRESULT STDMETHODCALLTYPE CopyTo(IMimeBody*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetTransmitInfo(LPTRANSMITINFO) = 0;
    virtual HRESULT STDMETHODCALLTYPE SaveToFile(ENCODINGTYPE, LPCSTR) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetHandle(LPHBODY) = 0;
};

struct IMimeMessageTree : public IPersistStreamInit {
    virtual HRESULT STDMETHODCALLTYPE GetMessageSource(IStream**, DWORD) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetMessageSize(ULONG*, DWORD) = 0;
    virtual HRESULT STDMETHODCALLTYPE LoadOffsetTable(IStream*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SaveOffsetTable(IStream*, DWORD) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetFlags(DWORD*) = 0;
    virtual HRESULT STDMETHODCALLTYPE Commit(DWORD) = 0;
    virtual HRESULT STDMETHODCALLTYPE HandsOffStorage() = 0;
    virtual HRESULT STDMETHODCALLTYPE BindToObject(const HBODY, REFIID, void**) = 0;
    virtual HRESULT STDMETHODCALLTYPE SaveBody(HBODY, DWORD, IStream*) = 0;
    virtual HRESULT STDMETHODCALLTYPE InsertBody(BODYLOCATION, HBODY, LPHBODY) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetBody(BODYLOCATION, HBODY, LPHBODY) = 0;
    virtual HRESULT STDMETHODCALLTYPE DeleteBody(HBODY, DWORD) = 0;
    virtual HRESULT STDMETHODCALLTYPE MoveBody(HBODY, BODYLOCATION) = 0;
    virtual HRESULT STDMETHODCALLTYPE CountBodies(HBODY, BOOL, ULONG*) = 0;
    virtual HRESULT STDMETHODCALLTYPE FindFirst(LPFINDBODY, LPHBODY) = 0;
    virtual HRESULT STDMETHODCALLTYPE FindNext(LPFINDBODY, LPHBODY) = 0;
    virtual HRESULT STDMETHODCALLTYPE ResolveURL(HBODY, LPCSTR, LPCSTR, DWORD, LPHBODY) = 0;
    virtual HRESULT STDMETHODCALLTYPE ToMultipart(HBODY, LPCSTR, LPHBODY) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetBodyOffsets(HBODY, LPBODYOFFSETS) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetCharset(HCHARSET*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetCharset(HCHARSET, CSETAPPLYTYPE) = 0;
    virtual HRESULT STDMETHODCALLTYPE IsBodyType(HBODY, IMSGBODYTYPE) = 0;
    virtual HRESULT STDMETHODCALLTYPE IsContentType(HBODY, LPCSTR, LPCSTR) = 0;
    virtual HRESULT STDMETHODCALLTYPE QueryBodyProp(HBODY, LPCSTR, LPCSTR, BOOL, BOOL) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetBodyProp(HBODY, LPCSTR, DWORD, LPPROPVARIANT) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetBodyProp(HBODY, LPCSTR, DWORD, LPCPROPVARIANT) = 0;
    virtual HRESULT STDMETHODCALLTYPE DeleteBodyProp(HBODY, LPCSTR) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetOption(const TYPEDID, LPCPROPVARIANT) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetOption(const TYPEDID, LPPROPVARIANT) = 0;
};

typedef struct tagWEBPAGEOPTIONS { DWORD cbSize; DWORD dwFlags; DWORD dwDelay; WCHAR wchQuote; } WEBPAGEOPTIONS, *LPWEBPAGEOPTIONS;

struct IMimeMessage : public IMimeMessageTree {
    virtual HRESULT STDMETHODCALLTYPE CreateWebPage(IStream*, LPWEBPAGEOPTIONS,
                                                    IMimeMessageCallback*, IMoniker**) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetProp(LPCSTR, DWORD, LPPROPVARIANT) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetProp(LPCSTR, DWORD, LPCPROPVARIANT) = 0;
    virtual HRESULT STDMETHODCALLTYPE DeleteProp(LPCSTR) = 0;
    virtual HRESULT STDMETHODCALLTYPE QueryProp(LPCSTR, LPCSTR, BOOL, BOOL) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetTextBody(DWORD, ENCODINGTYPE, IStream**, LPHBODY) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetTextBody(DWORD, ENCODINGTYPE, HBODY, IStream*, LPHBODY) = 0;
    virtual HRESULT STDMETHODCALLTYPE AttachObject(REFIID, void*, LPHBODY) = 0;
    virtual HRESULT STDMETHODCALLTYPE AttachFile(LPCSTR, IStream*, LPHBODY) = 0;
    virtual HRESULT STDMETHODCALLTYPE AttachURL(LPCSTR, LPCSTR, DWORD, IStream*, LPSTR*, LPHBODY) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetAttachments(ULONG*, LPHBODY*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetAddressTable(IMimeAddressTable**) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetSender(LPADDRESSPROPS) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetAddressTypes(DWORD, DWORD, LPADDRESSLIST) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetAddressFormat(DWORD, ADDRESSFORMAT, LPSTR*) = 0;
    virtual HRESULT STDMETHODCALLTYPE EnumAddressTypes(DWORD, DWORD, IMimeEnumAddressTypes**) = 0;
    virtual HRESULT STDMETHODCALLTYPE SplitMessage(ULONG, IMimeMessageParts**) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetRootMoniker(IMoniker**) = 0;
};

#endif  // __has_include(<mimeole.h>)

// IID used through BindToObject; defined locally on both header paths so no
// uuid import library is required (value per the published interface id).
inline constexpr IID kIidIMimeBody =
    {0xc558834c, 0x7f86, 0x11d0, {0x82, 0x52, 0x00, 0xc0, 0x4f, 0xd8, 0x5a, 0xb4}};
