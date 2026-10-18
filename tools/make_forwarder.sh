#!/bin/sh
# make_forwarder.sh -- NSP-форвардер ULTRAKILL NX для установки через DBI.
#   tools/make_forwarder.sh <prod.keys> [выходная папка]
# Форвардер -- nx-hbloader (forwarder/) с DEFAULT_NRO = sdmc:/switch/ultrakill/ultrakill_nx.nro:
# запускает загрузчик как приложение, с полной памятью, без подмены тайтла.
# Ключи используются только локально для шифрования NCA; в NSP их нет.
set -e
KEYS=${1:?нужен путь к prod.keys}
OUT=${2:-nsp-out}
ROOT=$(cd "$(dirname "$0")/.." && pwd)
W=$(mktemp -d)
mkdir -p "$W/exefs" "$W/control" "$OUT"
cp "$ROOT/forwarder/hbl.nso" "$W/exefs/main"
cp "$ROOT/forwarder/hbl.npdm" "$W/exefs/main.npdm"
nacptool --create "ULTRAKILL - NX" "Dimasick-git" "r18" "$W/control/control.nacp"
cp "$ROOT/forwarder/icon.jpg" "$W/control/icon_AmericanEnglish.dat"
(cd "$W" && hacbrewpack -k "$KEYS" --exefsdir exefs --controldir control --noromfs --nologo \
    --titleid 01005CE3B1A40000 --titlename "ULTRAKILL - NX" --titlepublisher "Dimasick-git")
cp "$W"/hacbrewpack_nsp/*.nsp "$OUT/ULTRAKILL_NX_forwarder.nsp"
rm -rf "$W"
echo "готово: $OUT/ULTRAKILL_NX_forwarder.nsp"
