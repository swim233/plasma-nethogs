#!/bin/sh
# Compiles the catalogues into the package. kpackagetool6 installs a package
# as-is and will not run gettext, so the .mo files are committed alongside the
# .po sources rather than generated at install time.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
domain=plasma_applet_io.github.cloudnyko.procnetmonitor

for po in "$here"/*.po; do
    lang=$(basename "$po" .po)
    dir="$here/../package/contents/locale/$lang/LC_MESSAGES"
    mkdir -p "$dir"
    msgfmt --check -o "$dir/$domain.mo" "$po"
    echo "built $lang"
done
