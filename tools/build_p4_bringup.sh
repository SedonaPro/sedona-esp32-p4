#!/bin/bash
set -euo pipefail
. "$HOME/esp/esp-idf-v6.0.3/export.sh"
cd "$(dirname "$0")/.."
python3 build.py fw --profile p4-tests
