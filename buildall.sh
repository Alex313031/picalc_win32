#!/bin/bash

# Copyright (c) 2026 Alex313031.
# Script to build multiple SIMD/debug variants of PiCalc Win32.

# =============================================================================
# Colors
# =============================================================================
YEL='\033[1;33m'
CYA='\033[1;96m'
RED='\033[1;31m'
GRE='\033[1;32m'
c0='\033[0m'
bold='\033[1m'
underline='\033[4m'

# =============================================================================
# Error handling
# =============================================================================
yell() { echo -e "$0: $*" >&2; }
die()  { yell "${RED}$* ${c0}"; exit 1; }

# =============================================================================
# Help
# =============================================================================
usage() {
  cat <<EOF
Usage: $(basename "$0") [OPTIONS]

Build multiple SIMD/debug variants of PiCalc Win32. Resulting binaries are
collected under ./Release/{x86,x64}/.

Options:
  -h, --help      Show this help and exit.
  -v, --verbose   Pass -v to every ninja invocation (build and clean).
  --clean         Run 'ninja -t clean' in each existing output dir, then exit
                  (does not build or copy binaries).

Without arguments, the script:
  1. creates the output dirs under \$THERE/out/
  2. copies the matching args.gn into each
  3. runs 'gn gen' for each
  4. builds each variant with ninja
  5. copies the resulting picalc_*.exe into Release/{x86,x64}/

Environment overrides:
  JOBS    -j value for ninja (default 32; NT4 variant overrides to 1).

Variants x86_NT4 and x64_DEBUG have args.gn generated but are skipped in
build + copy by default - remove them from SKIP_BUILD inside this script
to re-enable.
EOF
}

# =============================================================================
# Argument parsing
# =============================================================================
VERBOSE=""
CLEAN=0
while [[ $# -gt 0 ]]; do
  case "$1" in
    -h|--help)    usage; exit 0 ;;
    -v|--verbose) VERBOSE="-v" ;;
    --clean)      CLEAN=1 ;;
    *)            die "Unknown argument: $1 (use -h for help)" ;;
  esac
  shift
done

# =============================================================================
# Variant data
# Add a new variant: append to DIRS, then add matching ARGS_FOR and EXE_FOR
# entries. Optionally add a RENAME_TO entry if the destination filename
# should differ from the source. Optionally add to SKIP_BUILD / DEBUG_VARIANTS
# / JOBS_FOR if it needs special treatment.
# =============================================================================
DIRS=(
  x86_x87  x86_MMX  x86_SSE   x86_SSE2  x86_SSE3  x86_SSE41 x86_SSE42
  x86_NT4  x86_DEBUG
  x64_SSE2 x64_SSE3 x64_SSE41 x64_SSE42 x64_AVX   x64_AVX2  x64_AVX512
  x64_DEBUG
)
declare -A ARGS_FOR=(
  [x86_x87]=x86_x87.gn   [x86_MMX]=x86_mmx.gn   [x86_SSE]=x86_sse.gn
  [x86_SSE2]=x86_sse2.gn [x86_SSE3]=x86_sse3.gn [x86_SSE41]=x86_sse41.gn
  [x86_SSE42]=x86_sse42.gn
  [x86_NT4]=NT4.gn       [x86_DEBUG]=debug_x86.gn
  [x64_SSE2]=x64_sse2.gn [x64_SSE3]=x64_sse3.gn [x64_SSE41]=x64_sse41.gn
  [x64_SSE42]=x64_sse42.gn
  [x64_AVX]=x64_avx.gn   [x64_AVX2]=x64_avx2.gn [x64_AVX512]=x64_avx-512.gn
  [x64_DEBUG]=debug_x64.gn
)
declare -A EXE_FOR=(
  [x86_x87]=picalc_x87.exe     [x86_MMX]=picalc_mmx.exe
  [x86_SSE]=picalc_sse.exe     [x86_SSE2]=picalc_sse2.exe
  [x86_SSE3]=picalc_sse3.exe   [x86_SSE41]=picalc_sse41.exe
  [x86_SSE42]=picalc_sse42.exe
  [x86_NT4]=picalc_mmx.exe     [x86_DEBUG]=picalc_sse2_debug.exe
  [x64_SSE2]=picalc_sse2.exe   [x64_SSE3]=picalc_sse3.exe
  [x64_SSE41]=picalc_sse41.exe [x64_SSE42]=picalc_sse42.exe
  [x64_AVX]=picalc_avx.exe     [x64_AVX2]=picalc_avx2.exe
  [x64_AVX512]=picalc_avx512.exe
  [x64_DEBUG]=picalc_sse3_debug.exe
)
# Variants whose destination filename should differ from the source.
declare -A RENAME_TO=(
  [x86_NT4]=picalc_NT4.exe
)
# Per-variant ninja -j override (default applies otherwise).
declare -A JOBS_FOR=(
  [x86_NT4]=1   # NT4 toolchain is fragile in parallel
)
# Variants to skip in build + copy (gn gen still runs so their args are valid).
SKIP_BUILD=(x86_NT4 x64_DEBUG)
# Variants that should also get '-d stats' from ninja.
DEBUG_VARIANTS=(x86_DEBUG x64_DEBUG)

# =============================================================================
# Helpers
# =============================================================================
in_array() {
  local needle="$1"; shift
  for x in "$@"; do [[ "$x" == "$needle" ]] && return 0; done
  return 1
}
dumpdir_for() {
  # x86_* -> DUMPDIR32, x64_* -> DUMPDIR64
  case "$1" in
    x86_*) echo "$DUMPDIR32" ;;
    x64_*) echo "$DUMPDIR64" ;;
    *)     echo "$DUMPDIR"   ;;
  esac
}

# =============================================================================
# Phase functions
# =============================================================================
setup_paths() {
  printf "${GRE} Setting environment vars ${c0}\n"
  # Current dir (where this script lives, not where it was invoked). Capture
  # the absolute path BEFORE any cd so it doesn't depend on $0 being meaningful
  # after the directory change.
  export HERE=$(cd "$(dirname "$0")" && pwd)
  local here_abs="$HERE"
  cd "$HERE/../../" || die "cd to GN root failed"
  . ./set_env_vars  || die "Failed to source set_env_vars"
  export THERE="${PWD}"
  # set_env_vars overwrites HERE, so restore from the captured absolute path.
  export HERE="$here_abs"
  export DUMPDIR="${HERE}/Release"
  export DUMPDIR32="${DUMPDIR}/x86"
  export DUMPDIR64="${DUMPDIR}/x64"
  export JOBS="${JOBS:-32}"
  printf "${CYA} New path: $PATH ${c0}\n"
  sleep 1
}

make_dirs() {
  printf "${GRE} Making output directories... ${c0}\n"
  sleep 1
  mkdir -vp "$DUMPDIR" "$DUMPDIR32" "$DUMPDIR64" || die "mkdir dumpdirs failed"
  for d in "${DIRS[@]}"; do
    mkdir -vp "${THERE}/out/${d}" || die "mkdir ${d} failed"
  done
}

copy_args() {
  printf "${GRE} Copying args... ${c0}\n"
  sleep 1
  for d in "${DIRS[@]}"; do
    cp -fv "${HERE}/assets/args/${ARGS_FOR[$d]}" "${THERE}/out/${d}/args.gn" \
      || die "copy ${ARGS_FOR[$d]} failed"
  done
}

gn_gen() {
  printf "${GRE} Running gn gen out... ${c0}\n"
  sleep 1
  for d in "${DIRS[@]}"; do
    ./gn gen "out/${d}" || die "gn gen ${d} failed"
  done
}

do_build() {
  printf "${GRE} Building... ${c0}\n"
  sleep 1
  for d in "${DIRS[@]}"; do
    if in_array "$d" "${SKIP_BUILD[@]}"; then
      printf "${YEL}  Skipping build for ${d} ${c0}\n"
      continue
    fi
    local jobs="${JOBS_FOR[$d]:-$JOBS}"
    local extra=""
    if in_array "$d" "${DEBUG_VARIANTS[@]}"; then
      extra="-d stats"
    fi
    ./ninja -C "out/${d}" picalc_win32 -j"$jobs" $extra $VERBOSE \
      || die "ninja ${d} failed"
  done
}

do_copy_binaries() {
  printf "${GRE} Copying binaries to ${DUMPDIR}... ${c0}\n"
  sleep 1
  for d in "${DIRS[@]}"; do
    if in_array "$d" "${SKIP_BUILD[@]}"; then
      continue
    fi
    local src="${THERE}/out/${d}/${EXE_FOR[$d]}"
    local dst_dir
    dst_dir="$(dumpdir_for "$d")"
    local dst="$dst_dir"
    if [[ -n "${RENAME_TO[$d]:-}" ]]; then
      dst="${dst_dir}/${RENAME_TO[$d]}"
    fi
    cp -fv "$src" "$dst" || die "cp ${src} -> ${dst} failed"
  done
}

do_clean() {
  printf "${GRE} Cleaning all build directories... ${c0}\n"
  sleep 1
  for d in "${DIRS[@]}"; do
    if [[ -d "${THERE}/out/${d}" ]]; then
      printf "${CYA}  Cleaning out/${d} ${c0}\n"
      ./ninja -C "out/${d}" -t clean $VERBOSE \
        || yell "ninja -t clean failed for out/${d} (continuing)"
    else
      printf "${YEL}  Skipping out/${d} (does not exist) ${c0}\n"
    fi
  done
}

# =============================================================================
# Main
# =============================================================================
setup_paths

if (( CLEAN )); then
  do_clean
  printf "${GRE} Clean done! ${c0}\n"
  exit 0
fi

make_dirs
copy_args
gn_gen
do_build
do_copy_binaries

printf "${GRE} Done! ${c0}\n"
exit 0
