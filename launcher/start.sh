#!/usr/bin/env bash
# God Hand Launcher — Linux/macOS
set -e
cd "$(dirname "$0")"
exec python3 launcher.py "$@"
