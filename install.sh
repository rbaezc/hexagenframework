#!/bin/sh
set -e

# Installer script for Hexagen Framework (hf)
# Detects OS and CPU architecture, downloads the precompiled binary, and installs it.

REPO="rbaezc/hexagenframework"
VERSION="latest"

OS=$(uname -s | tr '[:upper:]' '[:lower:]')
ARCH=$(uname -m)

case "$OS" in
  darwin)
    PLATFORM="darwin"
    ;;
  linux)
    PLATFORM="linux"
    ;;
  *)
    echo "❌ Error: Unsupported Operating System: $OS"
    exit 1
    ;;
esac

case "$ARCH" in
  x86_64|amd64)
    BINARY_ARCH="x86_64"
    ;;
  arm64|aarch64)
    BINARY_ARCH="arm64"
    ;;
  *)
    echo "❌ Error: Unsupported CPU Architecture: $ARCH"
    exit 1
    ;;
esac

BINARY_NAME="hf"
DOWNLOAD_URL="https://github.com/$REPO/releases/$VERSION/download/${BINARY_NAME}_${PLATFORM}_${BINARY_ARCH}"

# Fallback to latest release details if specific version download fails
if [ "$VERSION" = "latest" ]; then
  DOWNLOAD_URL="https://github.com/$REPO/releases/latest/download/${BINARY_NAME}_${PLATFORM}_${BINARY_ARCH}"
fi

INSTALL_DIR="/usr/local/bin"
if [ ! -w "$INSTALL_DIR" ]; then
  echo "🔑 Need sudo permissions to write to $INSTALL_DIR"
  SUDO="sudo"
else
  SUDO=""
fi

echo "🏎️  Downloading Hexagen compiler ($BINARY_NAME) for ${PLATFORM}_${BINARY_ARCH}..."
TEMP_FILE=$(mktemp)

if command -v curl >/dev/null 2>&1; then
  curl -fsSL "$DOWNLOAD_URL" -o "$TEMP_FILE"
elif command -v wget >/dev/null 2>&1; then
  wget -qO "$TEMP_FILE" "$DOWNLOAD_URL"
else
  echo "❌ Error: curl or wget is required to run this install script."
  exit 1
fi

echo "⚙️  Installing to $INSTALL_DIR/$BINARY_NAME..."
$SUDO mv "$TEMP_FILE" "$INSTALL_DIR/$BINARY_NAME"
$SUDO chmod +x "$INSTALL_DIR/$BINARY_NAME"

echo "\n✨ Hexagen Framework ($BINARY_NAME) installed successfully!"
echo "👉 Run 'hf --help' to verify the installation."
