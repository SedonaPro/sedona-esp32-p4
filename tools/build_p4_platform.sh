#!/bin/bash
set -euo pipefail
. "$HOME/esp/esp-idf-v6.0.3/export.sh"
cd "$(dirname "$0")/.."
mkdir -p build
python3 - <<'PYGEN'
from pathlib import Path
import secrets,os
p=Path('build/p4-admin-password.txt')
if not p.exists():
    fd=os.open(p,os.O_WRONLY|os.O_CREAT|os.O_EXCL,0o600)
    with os.fdopen(fd,'w') as f:f.write(secrets.token_urlsafe(18)+'\n')
PYGEN
export SEDONA_ADMIN_PASSWORD="$(cat build/p4-admin-password.txt)"
if [[ -z "${SEDONA_APP_SOURCE:-}" && -f build/controller-base.sax ]]; then
  export SEDONA_APP_SOURCE="$PWD/build/controller-base.sax"
fi
python3 build.py sedona --profile p4
python3 build.py fw --profile p4
