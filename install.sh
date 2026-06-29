#!/bin/bash

echo "=== [kamputa] installer  ==="

if [ "$EUID" -ne 0 ]; then
    echo "Error: run this script as root!"
    echo "Example: su -c './install_kamputa.run'"
    exit 1
fi


REAL_USER=$(logname 2>/dev/null)

if [ -n "$REAL_USER" ] && [ "$REAL_USER" != "root" ]; then
    USERNAME="$REAL_USER"
else
    echo -n "Enter the username that will have access to kamputa: "
    read USERNAME
fi

if [ -z "$USERNAME" ]; then
    echo "Error: Username cannot be empty"
    exit 1
fi

TMPDIR=$(mktemp -d)

sed '1,/^__ARCHIVE_BELOW__/d' "$0" | tar -xzf - -C "$TMPDIR"

cd "$TMPDIR" || exit 1


echo "Compiling kamputa.c..."
gcc kamputa.c -o kamputa -lcrypt -O2

if [ $? -ne 0 ]; then
    echo "Compilation error! Make sure "gcc" is installed."
    rm -rf "$TMPDIR"
    exit 1
fi

echo "Installing binary to /usr/local/bin..."
mkdir -p /usr/local/bin
mkdir -p /etc

cp kamputa /usr/local/bin/kamputa

if [ ! -f "/etc/kamputa.conf" ]; then
    echo "$USERNAME" > /etc/kamputa.conf
    echo "Created config /etc/kamputa.conf. Added user: $USERNAME"
else
    if ! grep -q "^$USERNAME$" /etc/kamputa.conf; then
        echo "$USERNAME" >> /etc/kamputa.conf
        echo "User $USERNAME added to existing config."
    fi
fi


chown root:root /usr/local/bin/kamputa
chmod 4755 /usr/local/bin/kamputa

chown root:root /etc/kamputa.conf
chmod 644 /etc/kamputa.conf


rm -rf "$TMPDIR"

echo "=== [kamputa] Installation completed successfully! ==="
echo "User '$USERNAME' can now use the command: kamputa <command>"
exit 0

__ARCHIVE_BELOW__