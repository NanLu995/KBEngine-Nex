#!/bin/sh
set -e


# =========================================
# Build type
# =========================================
KBE_CONFIG=${1:-Release}
echo "[INFO] Using build type: $KBE_CONFIG"

# =========================================
# Basic tools and dependencies (macOS)
# =========================================
if ! command -v git >/dev/null 2>&1; then
    echo "[ERROR] Git is not installed."
    echo "[INFO] Please install Git first, then rerun this script."
    echo "[INFO] Official site: https://git-scm.com/download/mac"
    echo "[INFO] Tutorial: https://docs.github.com/en/get-started/git-basics/set-up-git"
    exit 1
fi

if ! command -v brew >/dev/null 2>&1; then
    echo "[ERROR] Homebrew is not installed."
    echo "[INFO] Please install Homebrew first, then rerun this script."
    echo "[INFO] Official site: https://brew.sh/"
    echo "[INFO] Install guide: https://docs.brew.sh/Installation"
    exit 1
fi

if ! command -v xcode-select >/dev/null 2>&1; then
    echo "[ERROR] xcode-select is not available."
    echo "[INFO] Please install Xcode Command Line Tools first, then rerun this script."
    echo "[INFO] Run: xcode-select --install"
    echo "[INFO] Apple docs: https://developer.apple.com/download/all/"
    exit 1
fi

if ! xcode-select -p >/dev/null 2>&1; then
    echo "[ERROR] Xcode Command Line Tools are not installed."
    echo "[INFO] Please install them first, then rerun this script."
    echo "[INFO] Run: xcode-select --install"
    echo "[INFO] Apple docs: https://developer.apple.com/download/all/"
    exit 1
fi

install_brew_dep() {
    PKG="$1"
    if brew list --formula "$PKG" >/dev/null 2>&1; then
        echo "[INFO] $PKG is already installed"
        return 0
    fi

    echo "[INFO] Installing $PKG via Homebrew..."
    brew install "$PKG"
}

install_brew_dep cmake
install_brew_dep autoconf
install_brew_dep automake
install_brew_dep libtool
install_brew_dep pkg-config
install_brew_dep libtirpc

# =========================================
# Check GitHub accessibility
# =========================================
echo "[CHECK] Trying to access GitHub repository..."
if ! git ls-remote https://github.com/microsoft/vcpkg.git >/dev/null 2>&1; then
    echo "[ERROR] Cannot access GitHub repository, please check network or proxy"
    echo "[INFO] You can also use the domestic Gitee mirror, try running gitee/install_linux.sh."
    exit 1
fi
echo "[SUCCESS] GitHub repository is accessible"



# =========================================
# Install vcpkg
# =========================================
VCPKG_DIR="$HOME/kbe-vcpkg"
if [ ! -d "$VCPKG_DIR" ] || [ ! -f "$VCPKG_DIR/bootstrap-vcpkg.sh" ]; then
    echo "[INFO] Cloning vcpkg"
    git clone https://github.com/microsoft/vcpkg.git "$VCPKG_DIR"
else
    echo "[INFO] vcpkg already exists: $VCPKG_DIR"
fi


git -C "$VCPKG_DIR" reset --hard HEAD
git -C "$VCPKG_DIR" pull


export CMAKE_MAKE_PROGRAM=$(which make)


OLDPWD=$(pwd)
cd "$VCPKG_DIR"
./bootstrap-vcpkg.sh
cd "$OLDPWD"


export CMAKE_MAKE_PROGRAM=$(which make)
# =========================================
# Build KBEngine-Nex
# =========================================
echo "[INFO] Entering ./kbe/src/"
cd "./kbe/src/"

echo "[INFO] Configuring CMake"
cmake -B build -S . \
    -DCMAKE_TOOLCHAIN_FILE="$VCPKG_DIR/scripts/buildsystems/vcpkg.cmake" \
    -DKBE_CONFIG="$KBE_CONFIG"

echo "[INFO] Building KBEngine-Nex"
cmake --build build -j"$(nproc)"

echo "[INFO] Installation complete"
