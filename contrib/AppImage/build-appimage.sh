#!/bin/bash

set -e
unset SOURCE_DATE_EPOCH

# Manually create the AppImage (reproducibly) since linuxdeployqt is not able to create cross-compiled AppImages

APPDIR="$PWD/biscuit.AppDir"

mkdir -p "$APPDIR"
mkdir -p "$APPDIR/usr/share/applications/"
mkdir -p "$APPDIR/usr/bin"

cp "src/assets/biscuit.desktop" "$APPDIR/usr/share/applications/biscuit.desktop"
cp "src/assets/biscuit.desktop" "$APPDIR/biscuit.desktop"
cp "src/assets/images/appicons/64x64.png" "$APPDIR/biscuit.png"
cp "build/bin/biscuit" "$APPDIR/usr/bin/biscuit"
chmod +x "$APPDIR/usr/bin/biscuit"
if [ -f "build/bin/biscuit-swapd" ]; then
    cp "build/bin/biscuit-swapd" "$APPDIR/usr/bin/biscuit-swapd"
    chmod +x "$APPDIR/usr/bin/biscuit-swapd"
fi

cp "contrib/AppImage/AppRun" "$APPDIR/"
chmod +x "$APPDIR/AppRun"

find biscuit.AppDir/ -exec touch -h -a -m -t 202101010100.00 {} \;

mksquashfs biscuit.AppDir biscuit.squashfs -comp zstd -info -root-owned -no-xattrs -noappend -fstime 0
# mksquashfs writes a timestamp to the header
printf '\x00\x00\x00\x00' | dd conv=notrunc of=biscuit.squashfs bs=1 seek=$((0x8))

rm -f biscuit.AppImage

cat /feather/contrib/depends/${HOST}/runtime >> biscuit.AppImage
cat biscuit.squashfs >> biscuit.AppImage
chmod a+x biscuit.AppImage
