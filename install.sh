#!/usr/bin/env bash
set -e

REPO="tedex228/Terminal-Render"
BIN_NAME="terminal-render"
INSTALL_DIR="/usr/local/bin"

URL="https://github.com/$REPO/releases/latest/download/$BIN_NAME"

echo "Downloading $BIN_NAME..."
if command -v curl &>/dev/null; then
    curl -#L "$URL" -o /tmp/$BIN_NAME
elif command -v wget &>/dev/null; then
    wget --show-progress -q "$URL" -O /tmp/$BIN_NAME
else
    echo "error: install curl or wget first"
    exit 1
fi

chmod +x /tmp/$BIN_NAME

echo "Installing to $INSTALL_DIR/$BIN_NAME..."
sudo mv /tmp/$BIN_NAME "$INSTALL_DIR/$BIN_NAME"

echo ""
echo "Installed! Run with: $BIN_NAME"
