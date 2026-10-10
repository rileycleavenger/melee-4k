# PS5 toolchain spike

Question: melee-pc's game C needs GCC (`scalar_storage_order`), while the PS5
toolchain (ps5-payload-sdk, as dusklight-ps5 uses it) is clang/lld. Can
GCC-built game objects live in a prospero-clang/lld PS5 executable?

## Result (2026-10-10, ps5-payload-sdk v0.42, x86_64-elf-gcc 16.2, llvm@18)

Build and link: **yes.**

- `gcc_sweep.py` compiles all 994 `melee_game` translation units with
  `x86_64-elf-gcc` against the SDK's FreeBSD libc headers: 994/994.
- All of them link with prospero-clang/lld into one PIE (6.8 MB) with only
  PIE-safe relocations (`RELATIVE`, `GLOB_DAT`, one `R_X86_64_64`).
- Of 489 undefined symbols, 63 come from the SDK libc and the rest are what
  aurora and `src/pc` provide (GX/OS/DVD/..., `pc_*`, `net_*`, FIO/MCC stubs).

ABI settings the GCC side needs, all in `build.sh` / `gcc_sweep.py`:

| Flag | Why |
|---|---|
| `-fshort-wchar` | PS5 `wchar_t` is 16-bit `unsigned short`; GCC defaults to 32-bit. Every other size (long, long double, pointers) already matches. |
| `-D__FreeBSD__=9 -D__PROSPERO__=1` | Predefines the SDK headers key on (prospero-clang defines them). |
| `-D__NO_TLS` | GCC emits native TLS, the console build uses `-femulated-tls`. FreeBSD `<ctype.h>` would otherwise inline a `_Thread_local` read (`lbcardnew.c`). With it, no game object has a TLS relocation. |
| `-fPIC -fno-plt -fno-stack-protector` | What prospero-clang passes. |
| `-nostdinc -isystem $SDK/target/include -isystem <gcc include>` | SDK libc first, GCC's own `stddef.h`/intrinsics after. |

Found, to fix in the real PS5 build:

- `src/sysdolphin/baselib/debug.c` defines `__assert`, which collides with
  FreeBSD libc's `__assert` in the SDK `libc.a` (hidden here with
  `--allow-multiple-definition`). Rename it on PS5.

Not yet verified: running on the console. `build.sh <mac-ip>` builds
`build/ps5-spike/spike.elf`, which checks big-endian disc structs, NULL-terminated
varargs, struct-by-value and callbacks across the GCC/clang boundary, mixed
int/float argument passing, `wchar_t` agreement, SDK libc from GCC code, and
mapping MEM1 at an address whose low 32 bits are `0x80000000`:

```sh
nc -l 9030                                   # on the Mac, before sending
ps5/spike/build.sh <mac-ip>
nc -w 3 <console-ip> 9021 < build/ps5-spike/spike.elf
```
