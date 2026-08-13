# Vendored SQLite

- Source: SQLite amalgamation, version **3.38.2** (`sqlite3.c` / `sqlite3.h`).
- License: public domain (https://sqlite.org/copyright.html).
- Why vendored: WLM2PST must build offline and deterministically for MSVC
  (Win32 + x64), MinGW cross-compilation, and native test builds; a single
  vendored amalgamation keeps the resume database engine identical everywhere.
- Build: compiled as the static library `wlm2pst_sqlite` with
  `SQLITE_OMIT_LOAD_EXTENSION` (no runtime extension loading) and
  `SQLITE_DQS=0` (standards-conforming quoting).

To upgrade, replace `sqlite3.c` and `sqlite3.h` with a newer amalgamation from
https://sqlite.org/download.html and update the version above.
