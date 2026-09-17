#!/usr/bin/env bash
# ============================================================================
#  divegram/dist.sh — универсальный сборщик пакетов DiveGram
#
#  ИЗ ОДНОГО собранного бинарника делает установочные пакеты для ВСЕХ
#  популярных дистрибутивов (НЕ пересобирая проект в каждом из них):
#
#    divegram_<ver>_amd64.deb     Debian / Ubuntu / Mint / Pop!_OS / eOS / Lemon
#    divegram-<ver>.x86_64.rpm     Fedora / RHEL / Rocky / Alma / openSUSE
#    divegram-<ver>-1-x86_64.pkg.tar.zst   Arch / Manjaro / EndeavourOS (makepkg)
#    divegram-<ver>-linux-x86_64.tar.xz    универсальный переносной (любой Linux)
#
#  Зависимости ДЛЯ ПАКЕТОВ (не для сборки): fpm (deb/rpm), base-devel+binutils
#  (маperepkg), tar+xz. Всё ставится в контейнере divegram_env сразу.
#
#  Использование:
#    ./dist.sh [путь-к-бинарию  DiveGram(байнер)] [версия]
#    по умолчанию:  DiveGram  из  out/Release/DiveGram , версия из
#    git describe (или "t$GIT_SHA")
# ============================================================================
set -euo pipefail

# ---------- пути и параметры ----------
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BIN="${1:-${ROOT}/out/Release/DiveGram}"
VERSION="${2:-$(git -C "$ROOT" describe --tags --always 2>/dev/null | tr -d 'v' || echo t$(git -C "$ROOT" rev-parse --short HEAD 2>/dev/null || echo 0git))}"
DIST="${ROOT}/dist"
PKGDIR="${DIST}/pkg-${VERSION}"

if [ ! -f "$BIN" ]; then
    echo "Бинарник не найден: $BIN" >&2
    echo "Сначала собери: см. docker build (README 'Сборка')." >&2
    exit 1
fi

# чистая рабочая папка пакетов
rm -rf "$PKGDIR"
mkdir -p "$PKGDIR/deb" "$PKGDIR/rpm" "$PKGDIR/arch"

echo "→ DiveGram: $(basename "$BIN") v$VERSION"
echo "→ $BIN"
file "$BIN" || true

# ---------- единая файловая разметка (из неё делаются все форматы) ----------
make_tree() {
    local root="$1"
    mkdir -p "$root/usr/bin" "$root/usr/share/applications" "$root/usr/share/icons/hicolor/512x512/apps"
    install -m0755 "$BIN" "$root/usr/bin/divegram"
    install -m0644 "${ROOT}/Telegram/Resources/icons/tg/icon_512.png" \
        "$root/usr/share/icons/hicolor/512x512/apps/divegram.png" 2>/dev/null || true
    cat > "$root/usr/share/applications/divegram.desktop" <<'EOF'
[Desktop Entry]
Name=DiveGram
Comment=DiveGram Desktop — вольный Linux-форк Telegram Desktop
Exec=divegram
Icon=divegram
Terminal=false
Type=Application
Categories=Network;InstantMessaging;Chat;
StartupWMClass=divegram
EOF
}

make_tree "$PKGDIR/root"

# ---------- 1) DEB ----------
echo "→ DEB: $DIST/divegram_${VERSION}_amd64.deb"
if command -v fpm >/dev/null 2>&1; then
    fpm -s dir -t deb -n divegram -v "$VERSION" -a amd64 \
        -p "$DIST/divegram_${VERSION}_amd64.deb" \
        --depends libc6 --depends libstdc++6 --depends libglib2.0-0 \
        --depends libpango-1.0-0 --depends libfontconfig1 --depends libfreetype6 \
        --depends zlib1g \
        -C "$PKGDIR/root" .
else
    echo "  fpm отсутствует — .deb пропущен (установи: apt install ruby && gem install fpm)" >&2
fi

# ---------- 2) RPM ----------
echo "→ RPM: $DIST/divegram-${VERSION}.x86_64.rpm"
if command -v fpm >/dev/null 2>&1; then
    fpm -s dir -t rpm -n divegram -v "$VERSION" -a x86_64 \
        -p "$DIST/divegram-${VERSION}.x86_64.rpm" \
        --depends glibc --depends libstdc++ --depends libpango \
        --depends fontconfig --depends freetype \
        -C "$PKGDIR/root" .
else
    echo "  fpm отсутствует — .rpm пропущен" >&2
fi

# ---------- 3) Arch: PKGBUILD + .pkg.tar.zst ----------
echo "→ Arch: $DIST/divegram-${VERSION}-1-x86_64.pkg.tar.zst + dist/PKGBUILD"
ARCHDIR="$PKGDIR/arch"
cat > "$ARCHDIR/PKGBUILD" <<EOF
# Maintainer: <твой ник> <твой@email>
# Contributor: <твой ник>

pkgname=divegram
pkgver=${VERSION}
pkgrel=1
epoch=
pkgdesc="DiveGram Desktop — свободный Linux-форк Telegram Desktop"
arch=('x86_64')
url="https://github.com/yak1tori/DiveGramDesktop"
license=('GPL3')
depends=('glibc' 'libstdc++' 'fontconfig' 'freetype2' 'pango')
makedepends=('cmake' 'ninja' 'base-devel')

source=("divegram-${VERSION}.tar.xz::file://${DIST}/divegram-${VERSION}-linux-x86_64.tar.xz")
md5sums=('SKIP')

package() {
    mkdir -p "\$pkgdir/usr/bin" "\$pkgdir/usr/share/applications" "\$pkgdir/usr/share/icons/hicolor/512x512/apps"
    install -Dm755 "\$srcdir/divegram" "\$pkgdir/usr/bin/divegram"
    install -Dm644 "\$srcdir/divegram.desktop" "\$pkgdir/usr/share/applications/divegram.desktop"
    install -Dm644 "\$srcdir/icon_512.png" "\$pkgdir/usr/share/icons/hicolor/512x512/apps/divegram.png"
    # библиотеки в зависимости поймали системные; если нужен полный самосодержащий пакет,
    # распакуй .tar.xz рядом и перенеси в opt/divegram + скрипт-лаунчер ld_library_path
}
EOF
cp "$ARCHDIR/PKGBUILD" "$DIST/PKGBUILD"

# собираем .tar.xz (он же используется PKGBUILD-ом выше как source)
echo "→ TAR: $DIST/divegram-${VERSION}-linux-x86_64.tar.xz"
mkdir -p "$PKGDIR/tarroot/divegram"
cp -r "$PKGDIR/root/." "$PKGDIR/tarroot/divegram/"
# эталонный переносной тоже
tar -C "$PKGDIR/tarroot" -cJf "$DIST/divegram-${VERSION}-linux-x86_64.tar.xz" divegram

# Arch-пакет собираем реально makepkg'ом (в каталоге с PKGBUILD + source на месте)
if command -v makepkg >/dev/null 2>&1; then
    echo "→ makepkg (реальный .pkg.tar.zst)…"
    mkdir -p "$PKGDIR/mkpkg" && cd "$PKGDIR/mkpkg"
    cp "$ARCHDIR/PKGBUILD" .
    cp "$DIST/divegram-${VERSION}-linux-x86_64.tar.xz" .
    makepkg -f 2>/dev/null # упакует в .pkg.tar.zst
    find . -maxdepth 1 -name '*.pkg.tar.zst' -exec mv {} "$DIST/" \;
    cd "$ROOT"
else
    echo "  makepkg отсутствует — .pkg.tar.zst пропущен (нужен base-devel на Arch)" >&2
fi

# ---------- итог ----------
echo
echo "══════════ ГОТОВО — пакеты в $DIST ══════════"
ls -lh "$DIST"/*.deb "$DIST"/*.rpm "$DIST"/*.pkg.tar.zst "$DIST"/*.tar.xz 2>/dev/null | awk '{print $9, "   (" $5 ")"}'
echo
# контрольные суммы для релиза
cd "$DIST"
sha256sum *.deb *.rpm *.pkg.tar.zst *.tar.xz > CHECKSUMS.txt # последний найдём в списке
mv CHECKSUMS.txt "${DIST}/CHECKSUMS.txt" 2>/dev/null || true
ls -lh "$DIST" | sed -n '3,15p'