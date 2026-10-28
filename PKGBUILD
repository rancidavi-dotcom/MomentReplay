pkgname=momentreplay
pkgver=0.1.0
pkgrel=1
pkgdesc='Native Linux instant replay, capture and clip library'
arch=('x86_64')
url='https://github.com/rancidavi-dotcom/MomentReplay'
license=('MIT')
depends=('qt6-base' 'qt6-declarative' 'qt6-multimedia' 'gstreamer' 'gst-plugins-base' 'gst-plugin-pipewire' 'sqlite')
makedepends=('cmake' 'git' 'pkgconf')
optdepends=('layer-shell-qt: native layer-shell integration when available')
source=("$pkgname::git+https://github.com/rancidavi-dotcom/MomentReplay.git#branch=main")
sha256sums=('SKIP')

pkgver() {
    cd "$srcdir/$pkgname"
    printf '0.1.0.r%s.g%s\n' "$(git rev-list --count HEAD)" "$(git rev-parse --short HEAD)"
}

build() {
    cmake -S "$srcdir/$pkgname" -B build \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/usr \
        -DBUILD_TESTING=OFF
    cmake --build build
}

check() {
    ctest --test-dir build --output-on-failure
}

package() {
    DESTDIR="$pkgdir" cmake --install build
    install -Dm644 "$srcdir/$pkgname/LICENSE" "$pkgdir/usr/share/licenses/$pkgname/LICENSE"
}
