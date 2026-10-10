#!/usr/bin/env bash
# Build the GCC/clang interop spike as a PS5 payload ELF.
#
#   ps5/spike/build.sh [HOST_IP]      HOST_IP: where `nc -l 9030` collects results
#
# Needs: ps5-payload-sdk (PS5_PAYLOAD_SDK, default .deps/ps5-payload-sdk-v0.42/
# ps5-payload-sdk), Homebrew llvm@18 (LLVM_CONFIG) and x86_64-elf-gcc.
# Send the result with `nc -w 3 <console> 9021 < build/ps5-spike/spike.elf`.
set -euo pipefail
cd "$(dirname "$0")/../.."

SDK=${PS5_PAYLOAD_SDK:-$PWD/.deps/ps5-payload-sdk-v0.42/ps5-payload-sdk}
export LLVM_CONFIG=${LLVM_CONFIG:-/opt/homebrew/opt/llvm@18/bin/llvm-config}
GCC=${GCC:-x86_64-elf-gcc}
OUT=build/ps5-spike
mkdir -p "$OUT"

# The game's C goes through GCC (scalar_storage_order) with the console's
# headers and ABI: SDK libc headers ahead of GCC's own, the predefines the SDK
# headers key on, 16-bit wchar_t, and the code-generation flags prospero-clang
# passes (no stack protector, no PLT, position independent).
PS5_GCC_FLAGS=(
    -O2 -std=gnu11 -fPIC -fno-plt -fno-stack-protector -fshort-wchar
    -ffunction-sections -fdata-sections
    -nostdinc -isystem "$SDK/target/include" -isystem "$("$GCC" -print-file-name=include)"
    -D__FreeBSD__=9 -D__PROSPERO__=1 -D__NO_TLS
)
"$GCC" "${PS5_GCC_FLAGS[@]}" -c ps5/spike/game.c -o "$OUT/game.o"

host_def=()
if [[ $# -ge 1 ]]; then
    host_def=(-DSPIKE_HOST="\"$1\"")
fi
"$SDK/bin/prospero-clang" -O2 -Wall "${host_def[@]}" -c ps5/spike/main.c -o "$OUT/main.o"
"$SDK/bin/prospero-clang" "$OUT/main.o" "$OUT/game.o" -o "$OUT/spike.elf"
echo "built $OUT/spike.elf"
