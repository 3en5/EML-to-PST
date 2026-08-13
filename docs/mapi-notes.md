# MAPI engineering notes

These are the non-obvious, provider-specific details behind
`src/worker/mapi/`, kept separate from ARCHITECTURE.md because they matter
mainly to someone modifying that code, not to someone using or building the
tool. All magic values referenced below live in one place:
`src/worker/mapi/mapi_constants.h`.

## MAPI DLL resolution: why not just link mapi32.dll

WLM2PST does not blindly trust the system MAPI stub (`mapi32.dll`) to load
the correct implementation. Windows' generic MAPI stub is a thin redirector
that can point at *whatever* the currently-registered "default MAPI client"
is — which is not guaranteed to be classic Outlook even on a machine where
classic Outlook is installed (another MAPI-aware application can have
claimed the default). Loading the stub naively risks silently running
against the wrong MAPI implementation, or none at all.

Instead, WLM2PST follows Microsoft's MAPI Stub Library guidance and reads
the *true* DLL path directly out of the registry:

```text
HKLM\SOFTWARE\Clients\Mail\Microsoft Outlook
    DllPathEx   (preferred; wide-path capable)
    DllPath     (fallback if DllPathEx is absent)
```

(`kMailClientKey`, `kMailClientDllPathEx`, `kMailClientDllPath` in
`mapi_constants.h`.) This resolves to Outlook's real MAPI provider DLL —
typically `olmapi32.dll` — and that specific path is what gets loaded, not
a generic name. Only if this registry-driven resolution comes up empty does
the code fall back to loading `mapi32.dll` (`kMapiStubDll`) by name, as a
last resort rather than a first choice.

## MSUPST MS vs. MSPST MS

Outlook registers two PST message service providers:

- **`MSPST MS`** — the legacy ANSI provider. Produces an ANSI-format PST,
  which cannot correctly represent Unicode content (Hebrew and other
  non-ASCII text in particular). WLM2PST never creates or opens this
  provider.
- **`MSUPST MS`** — the Unicode provider (`kUnicodePstServiceName`). This is
  the only PST service WLM2PST ever adds to a profile
  (`TemporaryProfile::create_pst_service`).

Both provider add paths are handled in `create_pst_service()`: the
preferred path is `IMsgServiceAdmin2::CreateMsgServiceEx`, which returns the
new service's `MAPIUID` directly with no race window. When
`IMsgServiceAdmin2` is not available on the installed MAPI stack (see
`mapi_compat.h` below), the code falls back to
`IMsgServiceAdmin::CreateMsgService` followed by a `GetMsgServiceTable`
table scan filtered on `PR_SERVICE_NAME_A == "MSUPST MS"` — reliable
because the profile is freshly created for this run and is guaranteed to
contain exactly one such service.

## `PR_PST_PATH_W` (`0x6700`, `PT_UNICODE`)

`PR_PST_PATH_W` is the profile-section property that tells the `MSUPST MS`
provider which file to open or create. It is **not** declared in any public
SDK header — `mapi_constants.h` defines it explicitly:

```cpp
#define PR_PST_PATH_W PROP_TAG(PT_UNICODE, 0x6700)
```

The tag ID (`0x6700`) matches MFCMAPI's own `MSPST.h` usage, which is the
best available public reference for undocumented PST provider properties.
`add_unicode_pst_service()` sets this alongside `PR_DISPLAY_NAME_W` and
calls `ConfigureMsgService` with `ulUIParam = 0` (no window handle) and no
`SERVICE_UI_ALWAYS` flag, so the provider creates a new Unicode PST at that
path with no UI prompt when the path does not already exist, and simply
opens it when it does (used for `--resume` and validation, where
`must_exist = true` is checked with `GetFileAttributesW` before the profile
is even created).

## `IConverterSession` vtable provenance

`IConverterSession` (MIME <-> MAPI conversion, CLSID
`{4E3A7680-B77A-11D0-9DA5-00C04FD65685}`, IID
`{4B401570-B77B-11D0-9DA5-00C04FD65685}`) is registered by classic Outlook
but its interface is not published in any Microsoft SDK header. The vtable
declaration in `mapi_constants.h` — including the unused `PlaceHolder1`
through `PlaceHolder5` slots, which must stay in place to keep later methods
at the correct vtable offset — is reconstructed from the Microsoft MFCMAPI
project (`core/interpret/guid.h` and its MIME-handling code) together with
Microsoft's documented "Importing MIME email with MAPI" sample. Do not
reorder or remove members of this class; doing so would silently call the
wrong virtual method against the real Outlook-provided object.

`MimeConverter::create()` obtains the interface with plain
`CoCreateInstance(kClsidIConverterSession, ..., kIidIConverterSession, ...)`
— the object is provided in-process by Outlook once MAPI is initialized, no
special activation is required.

## MAPIUID service lookup fallback

Covered above under MSUPST MS vs. MSPST MS: the `GetMsgServiceTable` scan
is the fallback path used only when `IMsgServiceAdmin2::CreateMsgServiceEx`
is unavailable. It exists purely for compatibility with older or
differently-packaged MAPI header/runtime combinations
(`WLM2PST_HAVE_IMSGSERVICEADMIN2` in `mapi_compat.h`); on any modern classic
Outlook install the direct `CreateMsgServiceEx` path is taken and the table
scan never runs.

## Buffer lifetime rules

Extended MAPI hands back memory the caller must free explicitly, and the
codebase follows two consistent rules to avoid leaks and double-frees:

- **`MAPIFreeBuffer` pairing.** Any `LPSPropValue`/`LPSRowSet`/similar
  pointer returned by a MAPI call (`GetProps`, `QueryRows`, etc.) is
  immediately wrapped in a small RAII guard (`MapiBuffer`, `RowSetGuard` in
  `src/worker/mapi/mapi_raii.h`) that calls `MAPIFreeBuffer` on scope exit.
  Buffers are never freed manually inline, and are never left unfreed on an
  early-return path — the guard's destructor runs regardless of which
  `return` statement is taken.
- **Row-set freeing.** `IMAPITable::QueryRows` results are freed as a whole
  row set (`RowSetGuard(rows, runtime.MAPIFreeBuffer)`), not per-row or
  per-property — freeing the row set's top-level pointer releases every
  property value it contains, and freeing anything nested inside it
  separately would be a double-free.

All MAPI interface pointers themselves (`IMAPITable`, `IMessage`, `IAttach`,
`IStream`, etc.) are held in `MapiPtr<T>`, a `Release()`-on-destruction
wrapper, so `QueryInterface`'d and returned interfaces are never leaked or
double-released either.

## `KEEP_OPEN_READONLY` save pattern

Every message save in the import path uses
`msg.SaveChanges(KEEP_OPEN_READONLY)` rather than the default
`FORCE_SAVE`/close semantics. This keeps the just-saved `IMessage` open
(read-only) afterward so its `PR_ENTRYID` can be read immediately
(`PstStore::message_entry_id`) without a second round-trip to reopen the
message — entry ids are needed right away both for the SQLite `IMPORTED`
row and, for fallback messages, for nothing further (the object is simply
released once its entry id has been captured).

## Single-threaded PST writes

All PST writes go through one worker thread. Extended MAPI sessions and the
objects opened from them are not safe to call concurrently from multiple
threads, and the PST provider in particular serializes writes internally —
attempting concurrent `SaveChanges` calls from multiple threads against the
same store risks corruption, not just contention. The spec's allowed
parallelism (source inventory, ahead-of-time SHA-256 hashing, non-MAPI
metadata inspection) is therefore scoped to work that happens *before* a
file reaches the MAPI import step; the import step itself — folder
creation, `MIMEToMAPI`, property writes, `SaveChanges` — is always serial on
the one thread that owns the MAPI session.
