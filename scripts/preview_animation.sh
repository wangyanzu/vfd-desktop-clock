#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
preview_dir=$(mktemp -d /tmp/vfd-animation.XXXXXX)
trap 'rm -rf "$preview_dir"' EXIT HUP INT TERM
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pedantic -Iinclude scripts/animation_preview.cpp -o "$preview_dir/preview"
"$preview_dir/preview" > "$preview_dir/frames.json"
python3 - "$preview_dir/frames.json" <<'PY'
import json
import sys
from pathlib import Path
raw = Path(sys.argv[1]).read_text()
data = json.loads(raw)
assert len(data) == 3 and all(len(scenes) == 9 for scenes in data.values())
for scenes in data.values():
    for scene in scenes:
        assert all(len(row) == 20 for frame in scene['frames'] for row in frame['rows'])
template = Path('scripts/animation_preview.html').read_text()
Path('docs/animation-preview.html').write_text(template.replace('__ANIMATION_DATA__', raw.strip()))
print('Generated docs/animation-preview.html (27 scene/motion combinations)')
PY
