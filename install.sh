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

# Download target variables
CLI_NAME="hf"
CORE_NAME="hf_core"

CLI_URL="https://github.com/$REPO/releases/$VERSION/download/${CLI_NAME}_${PLATFORM}_${BINARY_ARCH}"
CORE_URL="https://github.com/$REPO/releases/$VERSION/download/${CORE_NAME}_${PLATFORM}_${BINARY_ARCH}"

if [ "$VERSION" = "latest" ]; then
  CLI_URL="https://github.com/$REPO/releases/latest/download/${CLI_NAME}_${PLATFORM}_${BINARY_ARCH}"
  CORE_URL="https://github.com/$REPO/releases/latest/download/${CORE_NAME}_${PLATFORM}_${BINARY_ARCH}"
fi

INSTALL_DIR="/usr/local/bin"
if [ ! -w "$INSTALL_DIR" ]; then
  echo "🔑 Need sudo permissions to write to $INSTALL_DIR"
  SUDO="sudo"
else
  SUDO=""
fi

download_file() {
  url="$1"
  dest="$2"
  if command -v curl >/dev/null 2>&1; then
    curl -fsSL "$url" -o "$dest"
  elif command -v wget >/dev/null 2>&1; then
    wget -qO "$dest" "$url"
  else
    echo "❌ Error: curl or wget is required to run this install script."
    exit 1
  fi
}

echo "🏎️  Downloading Hexagen CLI ($CLI_NAME) for ${PLATFORM}_${BINARY_ARCH}..."
TEMP_CLI=$(mktemp)
download_file "$CLI_URL" "$TEMP_CLI"

echo "🏎️  Downloading Hexagen Compiler Core ($CORE_NAME) for ${PLATFORM}_${BINARY_ARCH}..."
TEMP_CORE=$(mktemp)
download_file "$CORE_URL" "$TEMP_CORE"

echo "⚙️  Installing binaries to $INSTALL_DIR..."
$SUDO mv "$TEMP_CLI" "$INSTALL_DIR/$CLI_NAME"
$SUDO chmod +x "$INSTALL_DIR/$CLI_NAME"

$SUDO mv "$TEMP_CORE" "$INSTALL_DIR/$CORE_NAME"
$SUDO chmod +x "$INSTALL_DIR/$CORE_NAME"

echo "\n✨ Hexagen Framework installed successfully!"
echo "👉 Run 'hf --help' to verify the installation."
