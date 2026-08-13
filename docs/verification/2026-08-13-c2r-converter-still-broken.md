# STILL BROKEN at `987d252`: the C2R fallback converter does not convert

Verified on `main` @ `987d252` ("Windows verification round: C2R MAPI fixes,
long paths, self-describing diagnostics"), rebuilt from source and run against
real classic Outlook.

**This is the one remaining blocker. Everything else in that commit works.**

## What was run

Target machine: classic Outlook **x64**, Microsoft 365 O365BusinessRetail,
Click-to-Run. 2,178 real `.eml` files, 313 folders, 3.0 GiB.

```
Imported normally:       2,178
Preserved as attachment: 0
Failed to read:          0
Folders created:         313
PST validation:          PASSED
exit code:               0
```

## What the new `[.dump]` diagnostic shows

The dump mode added in `987d252` is what made this objective — thank you, it is
exactly the missing capability. Its output:

Redacted below — the archive is privileged legal correspondence, so names,
addresses and folder names are replaced with placeholders. Structure, property
errors, flags and the encoded-word are verbatim.

```
=== folder '<root>\<account>\Sent Items' : 284 message(s)
  - class='IPM.Note' subject='' body[0..120]='From: "<sender name>" <...>
    To: =?utf-8?B?15HXk9eZ16fXlCDXoteR16jXmdeq?= <...>'
    flags=0x00000021 has_attach=false attach_count=0
    |reads: class:ok subject:err=0x8004010F body:ok
```

Read that carefully:

| Observation | Meaning |
|---|---|
| `subject:err=0x8004010F` | `MAPI_E_NOT_FOUND` — the message has **no subject property at all** |
| `body` begins `From: ... To: ...` | the **entire raw EML, headers included, became the body** |
| `=?utf-8?B?...?=` visible in the body | RFC 2047 encoded-words were never decoded — headers were never parsed |
| `has_attach=false attach_count=0` | **no attachments were created**, on messages whose source is `multipart/mixed` |

So `MIMEToMAPI` reports success and produces a message that is simply the source
file as plain text. Nothing is converted. Counts, folder structure, and
validation all pass, because none of them inspect semantics.

## Root cause is now pinpointed

The verbose log added in `987d252` names the acquisition path:

```
[VERBOSE] MIME converter acquired via DllGetClassObject fallback:
          C:\Program Files\Microsoft Office\Root\Office16\OUTLMIME.DLL
```

**The `IConverterSession` obtained from `OUTLMIME.DLL` via `DllGetClassObject`
is not a working converter.** It instantiates, it accepts `MIMEToMAPI`, it
returns success — and it does nothing.

That fallback (originally mine) is therefore not a fix. Before it, C2R machines
failed loudly with `OUTLOOK_UNAVAILABLE` (exit 14) and converted nothing. With
it, they "succeed" and produce a corrupt archive. **The current behavior is
worse than the original failure.**

## Suggested next steps

- Verify whether the object needs initialization before use — e.g. `SetAdrBook`,
  or a charset/encoding call — and whether `MIMEToMAPI`'s `HRESULT` is being
  checked as strictly as it should be (does it return `S_FALSE`?).
- Compare against a machine where `CoCreateInstance` **succeeds** (an MSI/perpetual
  Outlook install, or a C2R box where the class is registered) to confirm the
  registry-COM path produces correct output. That isolates "fallback object is
  wrong" from "our call sequence is wrong everywhere".
- Investigate whether the correct C2R route is activation through Outlook's
  registration-free COM / activation context rather than `DllGetClassObject`,
  and how MFCMAPI actually obtains the converter on a C2R install.

## Until it is fixed

**The fallback should refuse to run rather than convert.** Suggested guard: after
acquiring a converter through any non-registry path, convert a bundled fixture
that has a subject, Hebrew body text and one attachment, and abort with a clear
error unless subject, decoded body and `attach_count == 1` all come back
correct. That single self-test would have caught this before 2,178 messages were
written.

More generally, `PST validation: PASSED` must not be reportable for output like
the above. See the semantic checks proposed in `converter-corruption.md`.

## What did work at `987d252`

Worth recording, because these are now confirmed good on real hardware:

- unit tests: **338 / 338 pass** on both x86 and x64 (the long-path scanner test
  now passes — the `\\?\` change fixed it, and it no longer needs the machine
  policy);
- both presets build clean with warnings-as-errors;
- the launcher selects the correct worker on both a 32-bit and a 64-bit Outlook
  machine;
- the tool refuses to start while `wlmail.exe` is running, with a clear message;
- source files are never modified;
- `scripts/package.ps1` produces a complete `dist/`.
