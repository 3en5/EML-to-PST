// WLM2PST - Extended MAPI header inclusion + cross-toolchain compatibility.
//
// MinGW-w64 ships mapiaux.h (IMsgServiceAdmin2); Windows SDK / Outlook MAPI
// header sets differ in what they declare. Everything the tool needs that is
// not guaranteed by every header set is declared here, guarded, so both MSVC
// and MinGW builds compile from the same source (spec section 33.16).
#pragma once

#include "common/win_compat.h"

#include <mapidefs.h>
#include <mapiguid.h>
#include <mapitags.h>
#include <mapix.h>
#include <mapiutil.h>

#if __has_include(<mapiaux.h>)
#include <mapiaux.h>
#define WLM2PST_HAVE_IMSGSERVICEADMIN2 1
#else
// Declaration per Outlook MAPI headers (MSDN: IMsgServiceAdmin2, OLEGUID 0x20387).
#if !defined(MAPI_IMSGSERVICEADMIN_METHODS2)
#define WLM2PST_DECLARED_IMSGSERVICEADMIN2 1
DEFINE_OLEGUID(IID_IMsgServiceAdmin2, 0x00020387, 0, 0);
#undef INTERFACE
#define INTERFACE IMsgServiceAdmin2
DECLARE_MAPI_INTERFACE_(IMsgServiceAdmin2, IUnknown)
{
    BEGIN_INTERFACE
    MAPI_IUNKNOWN_METHODS(PURE)
    MAPI_IMSGSERVICEADMIN_METHODS(PURE)
    MAPIMETHOD(CreateMsgServiceEx)(THIS_ LPTSTR lpszService, LPTSTR lpszDisplayName,
                                   ULONG_PTR ulUIParam, ULONG ulFlags,
                                   LPMAPIUID lpuidService) IPURE;
};
#endif
#define WLM2PST_HAVE_IMSGSERVICEADMIN2 1
#endif

// Wide property tags occasionally absent from older mapitags.h variants.
#ifndef PR_DISPLAY_NAME_W
#define PR_DISPLAY_NAME_W PROP_TAG(PT_UNICODE, 0x3001)
#endif
#ifndef PR_MESSAGE_CLASS_W
#define PR_MESSAGE_CLASS_W PROP_TAG(PT_UNICODE, 0x001A)
#endif
#ifndef PR_SUBJECT_W
#define PR_SUBJECT_W PROP_TAG(PT_UNICODE, 0x0037)
#endif
#ifndef PR_BODY_W
#define PR_BODY_W PROP_TAG(PT_UNICODE, 0x1000)
#endif
#ifndef PR_ATTACH_LONG_FILENAME_W
#define PR_ATTACH_LONG_FILENAME_W PROP_TAG(PT_UNICODE, 0x3707)
#endif
#ifndef PR_ATTACH_FILENAME_W
#define PR_ATTACH_FILENAME_W PROP_TAG(PT_UNICODE, 0x3704)
#endif
#ifndef PR_ATTACH_MIME_TAG_W
#define PR_ATTACH_MIME_TAG_W PROP_TAG(PT_UNICODE, 0x370E)
#endif
#ifndef PR_SERVICE_NAME_A
#define PR_SERVICE_NAME_A PROP_TAG(PT_STRING8, 0x3D09)
#endif
#ifndef PR_DISPLAY_NAME_A
#define PR_DISPLAY_NAME_A PROP_TAG(PT_STRING8, 0x3001)
#endif
