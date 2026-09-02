// WLM2PST - builds MAPI one-off ENTRYIDs (spec: [MS-OXCDATA] 2.2.5.1).
//
// A recipient or sender row without an ENTRYID is *unresolved*: Outlook shows
// the display name, has no address object behind it, and Reply has nowhere to
// go. A one-off ENTRYID is what turns a name+address pair into a real,
// resolvable address entry.
//
// The byte layout is fixed and unforgiving - a wrong field width silently
// produces an ENTRYID that still lets Outlook display the address (it reads
// that from the row properties) while every operation that actually opens the
// address entry, such as Add to Contacts, fails with MAPI_E_NO_SUPPORT. It
// lives here, free of MAPI headers, so the layout can be asserted byte by byte
// in unit tests.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace wlm2pst {

// Builds a one-off ENTRYID for an SMTP address:
//
//   ULONG   flags            = 0
//   MAPIUID one-off provider = {A41F2B81-A3BE-1910-9D6E-00DD010F5402}
//   USHORT  version          = 0
//   USHORT  flags            = MAPI_ONE_OFF_UNICODE | MAPI_SEND_NO_RICH_INFO
//   WCHAR   display name, address type ("SMTP"), address - each NUL terminated
//
// Version and flags are two 16-bit fields, not one 32-bit field: writing them
// as a single little-endian ULONG puts the flags in the version slot and
// leaves the flags zero, which claims 8-bit strings for what follows.
std::vector<std::uint8_t> build_one_off_entryid(const std::wstring& display_name,
                                                const std::wstring& address);

}  // namespace wlm2pst
