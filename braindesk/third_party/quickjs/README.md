# QuickJS (vendored)

JavaScript engine for bd-web (M3). QuickJS-ng, the actively maintained
fork of Fabrice Bellard's QuickJS — chosen over Duktape (unmaintained
since 2022, ES5-era) for its ~ES2025 coverage at ~210 KiB of code.

- Version: v0.18.0 (released 2026-11-01)
- Source: https://github.com/quickjs-ng/quickjs/releases/tag/v0.18.0
- Tarball: https://github.com/quickjs-ng/quickjs/archive/refs/tags/v0.18.0.tar.gz
- SHA-256 of the tarball:
  `28bcd89aa3683fd2a946537e5ce3f11cc46b5cbf3383c7561aaef3df5ff9f1dd`
- License: MIT (see LICENSE in this directory)

Files taken from the release (library only — no CLI, no libc bindings,
no test262 harness):

    dtoa.c dtoa.h
    libregexp.c libregexp.h libregexp-opcode.h
    libunicode.c libunicode.h libunicode-table.h
    quickjs.c quickjs.h quickjs-atom.h quickjs-c-atomics.h quickjs-opcode.h
    cutils.h list.h
    builtin-array-fromasync.h builtin-iterator-zip.h
    builtin-iterator-zip-keyed.h

Nothing is patched. To upgrade, download the new release tarball and
replace these files, then update the version + SHA-256 above.

Build: the braindesk Makefile compiles these with `-O2` (no warnings
flags — vendored code) and links them into `bd-web` only, with
`-lm -lpthread -ldl`. The engine runtime is capped (see
`src/apps/bd_js.c`: 16 MiB JS heap, 256 KiB stack) to fit the
browser's ≤65 MiB RSS budget.
