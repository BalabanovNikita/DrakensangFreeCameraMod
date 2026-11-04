#!/usr/bin/env bash
# Builds the mod with clang + lld-link using Wine's Windows headers and import libraries
# (no MinGW needed):
#   dinput8.dll         loader: proxy + long-lived hooks
#   drakensang_mod.dll  core: all features, reloadable at runtime
# The DLLs go to build/ and dist/<game>/. Nothing is written to a game folder without --install.
#
# Usage: ./build.sh [core|loader|all] [--install GAME_DIR]... [--reload]
#   --install  copy the DLLs and data files of that game; an existing drakensang_mod.ini is kept
#   --reload   ask a running game in the --install folders to reload the core
# Toolchain: override with the environment variables CC, LLD_LINK, WINE_INCLUDE, WINE_LIB.
# Files are installed by rename, so a running game keeps its mapped copies intact.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
SRC="$ROOT/src"
OUT="$ROOT/build"
DIST="$ROOT/dist"
CC="${CC:-clang}"
LLD_LINK="${LLD_LINK:-lld-link}"
WINE_INCLUDE="${WINE_INCLUDE:-/usr/include/wine}"
WINE_LIB="${WINE_LIB:-/usr/lib/wine/i386-windows}"

WHAT=all
RELOAD=0
GAMES=()
while (($#)); do
    case "$1" in
        core|loader|all) WHAT=$1 ;;
        --reload) RELOAD=1 ;;
        --install)
            [[ $# -ge 2 && -d $2 ]] || { echo "--install needs an existing game folder" >&2; exit 1; }
            GAMES+=("$2"); shift ;;
        *) echo "usage: $0 [core|loader|all] [--install GAME_DIR]... [--reload]" >&2; exit 1 ;;
    esac
    shift
done

mkdir -p "$OUT/obj"

CFLAGS=(--target=i686-pc-windows-msvc -O2 -Wall -Wno-unused-function
        "-I$WINE_INCLUDE/msvcrt" "-I$WINE_INCLUDE/windows"
        -fno-stack-protector)
LIBS=("$WINE_LIB/libkernel32.a" "$WINE_LIB/libuser32.a" "$WINE_LIB/libucrtbase.a" "$WINE_LIB/libntdll.a")

compile() {  # compile <prefix> <files...> -> prints object paths
    local prefix=$1; shift
    for c in "$@"; do
        local o="$OUT/obj/${prefix}_$(basename "${c%.c}").o"
        "$CC" "${CFLAGS[@]}" -c "$c" -o "$o"
        echo "$o"
    done
}

install_file() {  # install_file <src> <dst>
    cp "$1" "$2.new"
    mv -f "$2.new" "$2"
}

if [[ $WHAT == all || $WHAT == loader ]]; then
    mapfile -t objs < <(compile loader "$SRC"/shared/*.c "$SRC"/loader/*.c)
    "$LLD_LINK" /dll /nodefaultlib /entry:DllMain /safeseh:no /dynamicbase \
        /def:"$SRC/loader/dinput8.def" /out:"$OUT/dinput8.dll" "${objs[@]}" "${LIBS[@]}"
    for d in "$DIST"/*/; do install_file "$OUT/dinput8.dll" "${d}dinput8.dll"; done
    echo "built $OUT/dinput8.dll"
fi

if [[ $WHAT == all || $WHAT == core ]]; then
    mapfile -t objs < <(compile core "$SRC"/shared/*.c "$SRC"/core/*.c)
    "$LLD_LINK" /dll /nodefaultlib /entry:DllMain /safeseh:no /dynamicbase \
        /out:"$OUT/drakensang_mod.dll" "${objs[@]}" "${LIBS[@]}"
    for d in "$DIST"/*/; do
        mkdir -p "${d}_mod"
        install_file "$OUT/drakensang_mod.dll" "${d}_mod/drakensang_mod.dll"
    done
    echo "built $OUT/drakensang_mod.dll"
fi

for g in ${GAMES[@]+"${GAMES[@]}"}; do
    if [[ -f $g/drakensanga1.exe ]]; then pack="$DIST/river-of-time"; else pack="$DIST/drakensang"; fi
    mkdir -p "$g/_mod"
    if [[ $WHAT == all || $WHAT == loader ]]; then install_file "$OUT/dinput8.dll" "$g/dinput8.dll"; fi
    if [[ $WHAT == all || $WHAT == core ]]; then install_file "$OUT/drakensang_mod.dll" "$g/_mod/drakensang_mod.dll"; fi
    for f in "$pack"/drakensang_mod_*.txt; do install_file "$f" "$g/$(basename "$f")"; done
    [[ -f $g/drakensang_mod.ini ]] || cp "$pack/drakensang_mod.ini" "$g/drakensang_mod.ini"
    echo "installed into $g"
    if ((RELOAD)); then touch "$g/_mod/reload"; fi
done
