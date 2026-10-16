"""Validate minimal desktop Kconfig against the installed SDK, without firmware build."""
from pathlib import Path
import json, os, re, subprocess, sys
import yaml
sys.stdout.reconfigure(encoding="utf-8", errors="replace")
ROOT = Path(__file__).resolve().parents[1]
PROJECT = ROOT
OUT = ROOT / ".tools/ouse-core-config-check"
OUT.mkdir(parents=True, exist_ok=True)
version = (PROJECT / "version.txt").read_text(encoding="utf-8").strip()
for directory in (ROOT/"components/ouse_core", ROOT/"boards/waveshare_lcd35", ROOT/"components/ouse_runtime"):
    manifest = yaml.safe_load((directory/"idf_component.yml").read_text(encoding="utf-8"))
    assert manifest["version"] == version
    assert all("brookesia" not in name.lower() for name in manifest["dependencies"])
api_header = (ROOT/"components/ouse_core/include/ouse_core.h").read_text(encoding="utf-8")
assert f'#define OUSE_CORE_VERSION "{version}"' in api_header
for directory in (ROOT/"components/ouse_core", ROOT/"boards/waveshare_lcd35", ROOT/"components/ouse_runtime", PROJECT/"main"):
    deps = yaml.safe_load((directory/"idf_component.yml").read_text(encoding="utf-8"))["dependencies"]
    assert all((spec if isinstance(spec, str) else spec["version"]).startswith("==") for spec in deps.values())
SDK = Path(os.environ.get("IDF_PATH", "D:/esp/v6.1/esp-idf"))
PYTHON = Path("D:/Espressif/python_env/idf6.1_py3.11_env/Scripts/python.exe")
cache = Path(os.environ.get("OUSE_CORE_COMPONENT_CACHE", str(PROJECT / "managed_components")))
assert (cache / "lvgl__lvgl").is_dir(), "Resolve pinned managed_components first, or set OUSE_CORE_COMPONENT_CACHE"
# Match the BSP/adapter version closure already pinned in the manifests.
managed = sorted(p for p in cache.iterdir() if p.is_dir() and p.name in {
    "lvgl__lvgl", "waveshare__esp32_p4_wifi6_touch_lcd_3_5", "espressif__esp_lvgl_adapter",
    "espressif__esp_lcd_st7796", "espressif__esp_lcd_touch_ft5x06", "espressif__esp_lcd_touch",
    "espressif__esp_codec_dev", "espressif__usb", "espressif__button", "espressif__knob",
    "espressif__cmake_utilities", "espressif__freetype", "espressif__esp_lv_fs",
    "espressif__esp_mmap_assets", "espressif__esp_lv_decoder", "espressif__esp_new_jpeg",
    "espressif__libpng", "espressif__zlib", "espressif__quickjs-ng",
    "espressif__esp_hosted", "espressif__esp_wifi_remote", "espressif__wifi_remote_over_eppp", "espressif__eppp_link", "espressif__esp_serial_slave_link", "joltwallet__littlefs",
})
directories = list((SDK / "components").iterdir()) + managed
for name, key in (("Kconfig", "COMPONENT_KCONFIGS_SOURCE_FILE"),
                  ("Kconfig.projbuild", "COMPONENT_KCONFIGS_PROJBUILD_SOURCE_FILE")):
    sources = [p / name for p in directories if (p / name).is_file()]
    (OUT / name).write_text("\n".join('source "'+p.as_posix()+'"' for p in sources)+"\n", encoding="utf-8")
env = {"IDF_TARGET":"esp32p4", "IDF_TOOLCHAIN":"gcc", "IDF_VERSION":"6.1.0",
       "IDF_PATH":SDK.as_posix(), "IDF_ENV_FPGA":"", "KCONFIG_PROMPTLESS_NO_WARN":"IDF_TARGET",
       "COMPONENT_KCONFIGS_SOURCE_FILE":(OUT/"Kconfig").as_posix(),
       "COMPONENT_KCONFIGS_PROJBUILD_SOURCE_FILE":(OUT/"Kconfig.projbuild").as_posix(),
       "ESP_IDF_VERSION":"6.1.0"}
(OUT / "config.env").write_text(json.dumps(env), encoding="utf-8")
p = subprocess.run([str(PYTHON), "-m", "kconfgen", "--kconfig", str(SDK/"Kconfig"),
    "--config", str(OUT/"sdkconfig.input"), "--defaults", str(PROJECT/"sdkconfig.defaults"),
    "--env-file", str(OUT/"config.env"), "--list-separator", "semicolon",
    "--output", "header", str(OUT/"sdkconfig.h"), "--output", "config", str(OUT/"sdkconfig.checked")],
    capture_output=True, text=True, errors="replace")
if p.returncode: print(p.stdout, p.stderr)
assert p.returncode == 0
text = (OUT/"sdkconfig.h").read_text(encoding="utf-8")
for key, value in {
    "ESP_PANIC_HANDLER_IRAM":"1", "ESP_COREDUMP_ENABLE_TO_UART":"1", "ESP_COREDUMP_CAPTURE_DRAM":"1", "ESP_COREDUMP_STACK_SIZE":"4096",
    "IDF_TARGET":'"esp32p4"', "ESP32P4_REV_MIN_FULL":"301",
    "ESP_DEFAULT_CPU_FREQ_MHZ":"400", "SPIRAM_SPEED":"200", "SPIRAM_XIP_FROM_PSRAM":"1",
    "SPIRAM_FETCH_INSTRUCTIONS":"1", "SPIRAM_RODATA":"1", "ESPTOOLPY_FLASHSIZE":'"16MB"',
    "LV_USE_FLEX":"1", "LV_USE_LABEL":"1", "LV_USE_BUTTON":"1", "LV_USE_SLIDER":"1", "LV_USE_BAR":"1", "LV_USE_SWITCH":"1",
    "LV_FONT_MONTSERRAT_24":"1", "LV_FONT_SOURCE_HAN_SANS_SC_14_CJK":"1",
    "LV_USE_TEXTAREA":"1", "LV_USE_KEYBOARD":"1", "LV_USE_BUTTONMATRIX":"1",
    "LV_USE_ARC":"1", "LV_USE_CHECKBOX":"1", "LV_USE_DROPDOWN":"1", "LV_USE_SPINNER":"1",
    "ESP_WIFI_REMOTE_ENABLED":"1", "ESP_WIFI_REMOTE_LIBRARY_HOSTED":"1", "SLAVE_IDF_TARGET_ESP32C6":"1",
    "ESP_HOSTED_SDIO_SLOT_1":"1", "ESP_HOSTED_SDIO_CLOCK_FREQ_KHZ":"25000", "ESP_HOSTED_SDIO_GPIO_RESET_SLAVE":"54", "LV_USE_IMAGE":"1", "LV_USE_SNAPSHOT":"1", "LV_DRAW_SW_COMPLEX":"1", "ESP_MAIN_TASK_STACK_SIZE":"8192", "LV_OS_NONE":"1",
}.items():
    assert "#define CONFIG_"+key+" "+value in text, key
for key in ("ESP_HOSTED_DFLT_TASK_FROM_SPIRAM", "ESP_HOSTED_HOST_RESTART_NO_COMMUNICATION_WITH_SLAVE", "ESP_HOSTED_TRANSPORT_RESTART_ON_FAILURE", "ESP_HOSTED_ENABLE_BT_NIMBLE", "ESP_LVGL_ADAPTER_ENABLE_FREETYPE", "ESP_LVGL_ADAPTER_ENABLE_DECODER",
            "ESP_LVGL_ADAPTER_ENABLE_FS", "ESP_LVGL_ADAPTER_LVGL_THREAD_STACK_IN_PSRAM"):
    assert "#define CONFIG_"+key+" 1" not in text, key
assert "CONFIG_BROOKESIA_" not in text
import csv
rows = list(csv.reader(line for line in (PROJECT/"partitions.csv").read_text().splitlines()
                       if line.strip() and not line.lstrip().startswith("#")))
assert [[cell.strip() for cell in row[:5]] for row in rows] == [
    ["nvs", "data", "nvs", "0x9000", "0x6000"],
    ["phy_init", "data", "phy", "0xf000", "0x1000"],
    ["factory", "app", "factory", "0x10000", "0x800000"],
    ["littlefs_data", "data", "littlefs", "0x810000", "0x7f0000"],
]
print("PASS real IDF Kconfig: P4 v3, CPU400/PSRAM200/XiP, native UI/fonts, optional decoders disabled")
print("PASS no Brookesia Kconfig dependency; existing partition offsets/sizes retained")
