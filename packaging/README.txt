WLM2PST - Windows Live Mail EML tree to Unicode PST
=====================================================

WHAT THIS IS
------------
WLM2PST converts a recursive folder of Windows Live Mail .eml files into
ONE Unicode .pst file you can open locally in classic Microsoft Outlook.
It has no other output format and no cloud/network destination.

REQUIREMENTS
------------
- Windows 10 or Windows 11.
- Classic Microsoft Outlook installed (desktop MSI or Click-to-Run), with
  Extended MAPI support. The new Outlook app (the "one Outlook" / Microsoft
  365 app) does NOT support Extended MAPI and cannot be used - if that is
  all you have installed, WLM2PST will tell you so and exit rather than
  attempt to run.
  No mail account needs to be configured in Outlook; WLM2PST creates and
  uses its own temporary, tool-owned profile and removes it when done.
- The Microsoft Visual C++ Redistributable matching this build's
  architecture (x86 and/or x64). These executables link the default MSVC
  dynamic runtime, not a statically-linked one, so the redistributable must
  already be present on the machine. Get it from:
  https://learn.microsoft.com/cpp/windows/latest-supported-vc-redist

FILES IN THIS PACKAGE
----------------------
  wlm2pst.exe                32-bit launcher - run this one.
  wlm2pst-worker-x86.exe     Win32 worker, used when Outlook is 32-bit.
  wlm2pst-worker-x64.exe     x64 worker, used when Outlook is 64-bit.
  THIRD-PARTY-NOTICES.txt    Third-party license notices.
  LICENSES\SQLITE.txt        SQLite public-domain statement.

wlm2pst.exe detects which Outlook is installed and its bitness, then
launches the matching worker automatically. You never run the worker
executables directly.

USAGE
-----
Basic conversion:

  wlm2pst.exe --source "C:\OldMail" --output "D:\Migration\OldMail.pst"

Custom root folder name inside the PST:

  wlm2pst.exe --source "C:\OldMail" --output "D:\Migration\OldMail.pst" --root-name "Sample old mail"

Resume an interrupted job (both the PST and its state database must
already exist from a previous run):

  wlm2pst.exe --source "C:\OldMail" --output "D:\Migration\OldMail.pst" --resume

Full option list:

  --source <directory>    Required. Recursive source folder of EML files.
  --output <file.pst>     Required. Path to a new PST file.
  --root-name <name>      Optional. Root folder name inside the PST.
                          Default: Windows Live Mail
  --resume                Resume a compatible interrupted job.
  --overwrite              Delete a previous tool-owned output and start again.
  --quiet                  Show only errors and final summary.
  --verbose                Show additional technical progress (no message content).
  --help                   Show help.
  --version                Show application, worker, architecture, and build versions.

WHERE OUTPUT LANDS
-------------------
Everything is written beside the --output path you choose, using its full
path as a prefix:

  <output>.pst                        the PST itself
  <output>.wlm2pst-state.sqlite       resume/crash-recovery database
  <output>.wlm2pst-report.json        machine-readable summary
  <output>.wlm2pst.log                run log
  <output>.wlm2pst-errors.csv         only if something failed or fell back

EXIT CODES (summary)
----------------------
   0  success, no warnings
   2  success, some files preserved as fallback attachments
   3  success, some source files could not be read
  10  invalid command-line arguments
  11  invalid source directory / no EML files found
  12  output already exists (or --overwrite refused: not tool-owned)
  13  insufficient free disk space
  14  classic Outlook / Extended MAPI unavailable
  15  worker/Outlook bitness mismatch
  16  Unicode PST provider could not be created/configured
  17  estimated output exceeds the safe single-PST size ceiling
  18  disk full / output write failure during conversion
  19  PST validation failed
  20  state database incompatible with PST or source
  21  source changed after the manifest was created
  22  integrity error during resume recovery
  23  temporary profile creation/cleanup failed critically
 130  cancelled with Ctrl+C
See the full README.md in the source repository for the complete table
with descriptions.

PRIVACY
-------
Logs, the JSON report, and the errors CSV never contain message subjects,
bodies, addresses, raw MIME headers, or attachment content - only relative
paths, sizes, hashes, statuses, and technical codes, even at --verbose.

OPENING THE PST IN CLASSIC OUTLOOK
-------------------------------------
Once conversion finishes, open the resulting .pst directly - no import
wizard needed:

  File -> Open & Export -> Open Outlook Data File

Browse to the .pst file, and the migrated folders appear under the root
folder name you chose (or "Windows Live Mail" by default).

--------------------------------------------------------------------------
Developed by KALIT (https://kalit.co.il) - the development arm of
Natanor IT Services (https://natanor.co.il)
Licensed under the MIT License. See THIRD-PARTY-NOTICES.txt for the
components WLM2PST builds on.
