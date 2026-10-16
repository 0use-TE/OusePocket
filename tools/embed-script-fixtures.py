"""Generate native test fixtures; the firmware does not embed these apps."""
from pathlib import Path
import argparse
import json
import re

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument("--output", type=Path, required=True)
options = parser.parse_args()
out = options.output
out.mkdir(parents=True, exist_ok=True)
header = ['#pragma once', '#include "ouse_runtime.h"']
source = ['// Generated host-only fixtures; do not edit.', '#include "script_packages.h"']
fixtures = [("counter", "examples/counter", "ouse.counter", "LV_SYMBOL_PLUS"),
            ("resource-monitor", "apps/resource-monitor", "ouse.resources", "LV_SYMBOL_LIST"),
            ("api-test", "apps/api-test", "ouse.api-test", "LV_SYMBOL_EYE_OPEN")]
for name, path, app_id, icon in fixtures:
    directory = ROOT / path
    manifest = json.loads((directory / "manifest.json").read_text(encoding="utf-8"))
    assert manifest["api"] == 1 and re.fullmatch(r"[a-z0-9_.-]{1,47}", manifest["id"])
    symbol = name.replace("-", "_")
    header.append(f'extern const ouse_script_package_t ouse_package_{symbol};')
    data = {}
    for key, filename in [("page", "page.json"), ("script", "app.js")]:
        text = (directory / filename).read_text(encoding="utf-8")
        assert len(text.encode("utf-8")) <= 32768 and "\x00" not in text
        if key == "page":
            page = json.loads(text)
            assert page["api"] == 1
        # Escaped UTF-8 bytes avoid compiler execution charset and hex-escape bleed.
        value = '"' + ''.join(f'\\{byte:03o}' for byte in text.encode("utf-8")) + '"'
        source.append(f'static const char {symbol}_{key}[] = {value};')
        data[key] = f'{symbol}_{key}'
    # A tiny compiled RGB565 image exercises image assets, no decoder/filesystem.
    if name == "counter":
        pixels = [0x1290 if (x//4+y//4) % 2 else 0x4dff for y in range(16) for x in range(16)]
        source += ['static const uint16_t badge_pixels[] = {' + ','.join(hex(p) for p in pixels) + '};',
                   'static const lv_image_dsc_t badge = {.header={.magic=LV_IMAGE_HEADER_MAGIC,.cf=LV_COLOR_FORMAT_RGB565,.w=16,.h=16,.stride=32},.data_size=sizeof(badge_pixels),.data=(const uint8_t *)badge_pixels};',
                   'static const ouse_script_image_t counter_images[] = {{.id="badge",.image=&badge}};']
    source.append(f'const ouse_script_package_t ouse_package_{symbol} = {{' +
                  f'.id={json.dumps(app_id)},.name="' + ''.join(f'\\{b:03o}' for b in manifest['name'].encode()) +
                  f'",.icon={icon},.api=1,.page={data["page"]},.script={data["script"]}' +
                  (',.images=counter_images,.image_count=1' if name == 'counter' else '') + '};')
for path, content in [(out/"script_packages.h", header), (out/"script_packages.c", source)]:
    text = "\n".join(content) + "\n"
    if not path.exists() or path.read_text(encoding="utf-8") != text:
        path.write_text(text, encoding="utf-8")
print("Generated host-only script fixtures")
