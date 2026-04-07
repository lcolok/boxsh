#!/bin/sh
# boxsh installer — download the latest release binary for your platform.
#
# Usage:
#   curl -fsSL https://raw.githubusercontent.com/xicilion/boxsh/master/install.sh | sh
#
# Options (via environment variables):
#   BOXSH_VERSION   — specific version tag (default: latest)
#   BOXSH_INSTALL   — installation directory (default: /usr/local/bin)

set -e

REPO="xicilion/boxsh"
INSTALL_DIR="${BOXSH_INSTALL:-/usr/local/bin}"

# Detect OS
OS="$(uname -s)"
case "$OS" in
    Linux)  OS_TAG="linux" ;;
    Darwin) OS_TAG="darwin" ;;
    *)      echo "Error: unsupported OS: $OS" >&2; exit 1 ;;
esac

# Detect architecture
ARCH="$(uname -m)"
case "$ARCH" in
    x86_64|amd64)   ARCH_TAG="x64" ;;
    i686|i386)       ARCH_TAG="ia32" ;;
    aarch64|arm64)   ARCH_TAG="arm64" ;;
    armv7l|armhf)    ARCH_TAG="arm" ;;
    mips64*)         ARCH_TAG="mips64" ;;
    ppc64le)         ARCH_TAG="ppc64" ;;
    riscv64)         ARCH_TAG="riscv64" ;;
    loongarch64)     ARCH_TAG="loong64" ;;
    *)               echo "Error: unsupported architecture: $ARCH" >&2; exit 1 ;;
esac

# macOS uses different arch tags in uname vs release
if [ "$OS_TAG" = "darwin" ] && [ "$ARCH_TAG" = "x64" ]; then
    ARCH_TAG="x86_64"
fi

# Resolve version
if [ -z "$BOXSH_VERSION" ]; then
    BOXSH_VERSION="$(curl -fsSL "https://api.github.com/repos/${REPO}/releases/latest" \
        | grep '"tag_name"' | head -1 | sed 's/.*"tag_name": *"//;s/".*//')"
    if [ -z "$BOXSH_VERSION" ]; then
        echo "Error: failed to determine latest version" >&2
        exit 1
    fi
fi

FILENAME="boxsh-${BOXSH_VERSION}-${OS_TAG}-${ARCH_TAG}"
URL="https://github.com/${REPO}/releases/download/${BOXSH_VERSION}/${FILENAME}"

echo "Installing boxsh ${BOXSH_VERSION} (${OS_TAG}/${ARCH_TAG})..."
echo "  from: ${URL}"
echo "  to:   ${INSTALL_DIR}/boxsh"

# Download
TMP="$(mktemp)"
trap 'rm -f "$TMP"' EXIT
if ! curl -fSL -o "$TMP" "$URL"; then
    echo "Error: download failed. Check that the version and architecture are correct." >&2
    exit 1
fi
chmod +x "$TMP"

# Install
if [ -w "$INSTALL_DIR" ]; then
    mv "$TMP" "${INSTALL_DIR}/boxsh"
else
    echo "  (need sudo to write to ${INSTALL_DIR})"
    sudo mv "$TMP" "${INSTALL_DIR}/boxsh"
fi

echo "Done! Run 'boxsh --help' to get started."
