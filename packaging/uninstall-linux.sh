#!/usr/bin/env bash
set -euo pipefail

app_id='io.github.rancidavi_dotcom.moment'
confirm=${1:-}

cat <<'INFO'
Moment uninstaller

This removes Moment's user-level app binary, desktop launchers, icon files and
user-installed Flatpak package. Video clips, screenshots, preferences and
library data are intentionally kept.
INFO

if [[ "$confirm" != '--yes' ]]; then
    read -r -p 'Continue uninstalling Moment for this user? [y/N] ' answer
    [[ "$answer" == [yY] || "$answer" == [yY][eE][sS] ]] || {
        printf 'Cancelled.\n'
        exit 0
    }
fi

if command -v flatpak >/dev/null 2>&1 \
    && flatpak list --user --app --columns=application 2>/dev/null | grep -Fxq "$app_id"; then
    flatpak uninstall --user --assumeyes "$app_id"
fi

rm -f -- "$HOME/.local/bin/medal-linux"
rm -f -- "$HOME/.local/share/applications/io.moment.Moment.desktop"
rm -f -- "$HOME/.local/share/applications/io.github.rancidavi_dotcom.moment.desktop"
rm -f -- "$HOME/.local/share/icons/hicolor/scalable/apps/io.moment.Moment.svg"
rm -f -- "$HOME/.local/share/icons/hicolor/1024x1024/apps/io.moment.Moment.png"

if command -v pacman >/dev/null 2>&1 && pacman -Qq momentreplay >/dev/null 2>&1; then
    printf '\nThe system package momentreplay is installed. Remove it with: sudo pacman -Rns momentreplay\n'
fi

printf '\nMoment was removed for this user. Clips and settings were kept.\n'
