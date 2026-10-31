#!/usr/bin/env bash
# Run from the repository root in the MSYS2 UCRT64 shell.
set -euo pipefail
build_dir="${1:-build-windows}"
destination="dist/Moment"
if [[ -e "$destination" ]]; then
    echo "Refusing to overwrite $destination; use a fresh packaging directory." >&2
    exit 1
fi
mkdir -p "$destination/licenses"
cmake --install "$build_dir" --prefix "$PWD/$destination"
windeployqt6 --release --qmldir "$PWD/qml" "$PWD/$destination/Moment.exe"
cp /ucrt64/bin/ffmpeg.exe /ucrt64/bin/ffprobe.exe "$destination/"

# Resolve transitive MinGW dependencies for the app, FFmpeg and Qt plugins.
# Do not copy Windows system DLLs or depend on a user's MSYS2 installation.
declare -A scanned=()
declare -A packages=()
declare -A binary_owners=()
while read -r owner path; do
    binary_owners["${path##*/}"]="$owner"
done < <(pacman -Ql | awk '$2 ~ /^\/ucrt64\/bin\/.*\.(dll|exe)$/ {print $1, $2}')
mapfile -d '' queue < <(find "$destination" -type f \( -iname '*.exe' -o -iname '*.dll' \) -print0)
for ((i=0; i<${#queue[@]}; ++i)); do
    binary="${queue[i]}"
    [[ -z "${scanned[$binary]:-}" ]] || continue
    scanned[$binary]=1
    echo "Resolving runtime dependencies: $binary"
    owner="${binary_owners[${binary##*/}]:-}"
    [[ -z "$owner" ]] || packages[$owner]=1
    while IFS= read -r dll; do
        dll="${dll%$'\r'}"
        if [[ -f "/ucrt64/bin/$dll" ]]; then
            if [[ ! -f "$destination/$dll" ]]; then
                cp "/ucrt64/bin/$dll" "$destination/"
                queue+=("$destination/$dll")
            fi
        elif [[ "${dll,,}" != api-ms-* && "${dll,,}" != ext-ms-* && ! -f "$(cygpath -u "$SYSTEMROOT")/System32/$dll" ]]; then
            echo "Unresolved dependency in $binary: $dll" >&2
            exit 1
        fi
    done < <(objdump -p "$binary" | sed -n 's/^[[:space:]]*DLL Name: //p')
done

git rev-parse HEAD > "$destination/SOURCE-REVISION.txt"
cp LICENSE "$destination/LICENSE-Moment.txt"
cp packaging/windows/START-HERE.txt "$destination/START-HERE.txt"
echo "Copying license notices for ${#packages[@]} dependency packages"
pacman -Qi "${!packages[@]}" > "$destination/licenses/packages.txt"
while IFS= read -r license; do
    [[ -f "$license" ]] || continue
    relative="${license#/ucrt64/share/}"
    mkdir -p "$destination/licenses/$(dirname "$relative")"
    cp "$license" "$destination/licenses/$relative"
done < <(pacman -Qlq "${!packages[@]}" | sed -n '\|/share/licenses/|p')
cat > "$destination/licenses/SOURCES.txt" <<'EOF'
Moment source: https://github.com/rancidavi-dotcom/MomentReplay
Dependency package versions and upstream URLs: packages.txt in this directory.
MSYS2 build recipes and patches: https://github.com/msys2/MINGW-packages
MSYS2 source package archive: https://mirror.msys2.org/mingw/sources/
Qt: https://code.qt.io/ ; FFmpeg: https://ffmpeg.org/download.html
This is a development build. Moment's MIT license does not replace the licenses
of Qt, FFmpeg or their dependencies. Shared libraries can be replaced in this
folder. Keep the accompanying license notices when redistributing.
EOF
echo "Writing portable build checksums"
find "$destination" -type f -print0 | sort -z | xargs -0 sha256sum > dist/Moment-Windows-x64.sha256
