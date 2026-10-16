"""Exercise the real panic overlay and capture code on the host; never build firmware."""
from pathlib import Path
import hashlib
import json
import os
import re
import subprocess
import sys

sys.stdout.reconfigure(encoding="utf-8", errors="replace")
ROOT = Path(__file__).resolve().parents[1]
SDK = Path(os.environ.get("IDF_PATH", "D:/esp/v6.1/esp-idf"))
OUT = ROOT / ".tools/fault-diagnostics-check"
OUT.mkdir(parents=True, exist_ok=True)
CM = Path("D:/Espressif/tools/cmake/4.0.3/bin/cmake.exe")
NINJA = Path("D:/Espressif/tools/ninja/1.12.1/ninja.exe")
OVERLAY = ROOT / "boards/waveshare_lcd35/cmake/p4_fault_diagnostics.cmake"
CAPTURE = ROOT / "boards/waveshare_lcd35/diagnostics/p4_fault_snapshot.inc"
paths = {
    "panic_handler.c": SDK / "components/esp_system/port/panic_handler.c",
    "panic_arch.c": SDK / "components/esp_system/port/arch/riscv/panic_arch.c",
    "core_dump_uart.c": SDK / "components/espcoredump/src/core_dump_uart.c",
    "core_dump_elf.c": SDK / "components/espcoredump/src/core_dump_elf.c",
}
originals = {name: path.read_bytes() for name, path in paths.items()}
hashes = {name: hashlib.sha256(content).hexdigest() for name, content in originals.items()}
fixture = OUT / "fixture"
fixture.mkdir(exist_ok=True)
for name, content in originals.items():
    (fixture / name).write_bytes(content)
(fixture / "other.c").write_text("void retained(void) {}\n")
vswhere = Path(os.environ["ProgramFiles(x86)"]) / "Microsoft Visual Studio/Installer/vswhere.exe"
vs = subprocess.check_output([str(vswhere), "-latest", "-products", "*", "-requires",
    "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-property", "installationPath"], text=True).strip()
envscript = OUT / "environment.cmd"
envscript.write_text(f'@echo off\ncall "{vs}/VC/Auxiliary/Build/vcvars64.bat" >nul\nset\n', encoding="mbcs")
env = dict(os.environ)
for line in subprocess.check_output(["cmd.exe", "/d", "/c", str(envscript)], encoding="mbcs").splitlines():
    if "=" in line and not line.startswith("="):
        key, value = line.split("=", 1)
        env[key] = value
cc = Path(env["VCToolsInstallDir"]) / "bin/Hostx64/x64/cl.exe"

project = r'''
cmake_minimum_required(VERSION 3.24)
project(fault_overlay C)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CONFIG_IDF_TARGET_ESP32P4 y)
set(CONFIG_ESP32P4_REV_MIN_FULL 301)
set(CONFIG_ESP_PANIC_HANDLER_IRAM y)
set(CONFIG_ESP_COREDUMP_ENABLE_TO_UART y)
set(CONFIG_ESP_COREDUMP_CAPTURE_DRAM y)
set(CONFIG_ESP_COREDUMP_STACK_SIZE 4096)
add_library(system STATIC "FIXTURE/panic_handler.c" "FIXTURE/panic_arch.c" "FIXTURE/other.c")
add_library(dump STATIC "FIXTURE/core_dump_uart.c" "FIXTURE/core_dump_elf.c")
set_source_files_properties("FIXTURE/panic_arch.c" PROPERTIES COMPILE_FLAGS "/Od")
function(idf_component_get_property output component property)
 if(property STREQUAL "COMPONENT_LIB")
  if(component STREQUAL "espcoredump")
   set(${output} dump PARENT_SCOPE)
  else()
   set(${output} system PARENT_SCOPE)
  endif()
 else()
  set(${output} "FIXTURE" PARENT_SCOPE)
 endif()
endfunction()
include("OVERLAY")
get_target_property(actual system SOURCES)
file(WRITE "${CMAKE_BINARY_DIR}/selected.txt" "${actual}")
get_target_property(dump_actual dump SOURCES)
file(WRITE "${CMAKE_BINARY_DIR}/dump-selected.txt" "${dump_actual}")
'''.replace("FIXTURE", fixture.as_posix()).replace("OVERLAY", OVERLAY.as_posix())
(OUT / "CMakeLists.txt").write_text(project)

def configure(ok=True):
    p = subprocess.run([str(CM), "-S", str(OUT), "-B", str(OUT / "generated"),
        "-G", "Ninja", "-DCMAKE_MAKE_PROGRAM=" + str(NINJA),
        "-DCMAKE_C_COMPILER=" + str(cc), "-DCMAKE_BUILD_TYPE=Release",
        "-DCMAKE_TRY_COMPILE_CONFIGURATION=Release"], env=env, capture_output=True, text=True, errors="replace", timeout=90)
    assert (p.returncode == 0) == ok, (p.stdout, p.stderr)

configure()
generated = OUT / "generated/ouse_fault"
handler = (generated / "panic_handler.c").read_text()
arch = (generated / "panic_arch.c").read_text()
uart = (generated / "core_dump_uart.c").read_text()
assert (OUT / "generated/dump-selected.txt").read_text().split(";") == [(generated/name).as_posix() for name in ("core_dump_uart.c","core_dump_elf.c")]
elf_source=(generated/"core_dump_elf.c").read_text()
assert elf_source.count("heap_caps_walk(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT, esp_core_dump_write_heap_blocks, &user_data);")==1
assert "heap_caps_walk(MALLOC_CAP_8BIT," not in elf_source
print("PASS ELF dump capability filter: internal 8-bit heap only; sizing/header/data share the same walker")
assert "while (addr < end) {\n        esp_panic_handler_feed_wdts();" in uart
wdt_declaration = "extern void esp_panic_handler_feed_wdts(void);"
assert uart.index(wdt_declaration) < uart.index("        esp_panic_handler_feed_wdts();")
assert wdt_declaration in originals["panic_handler.c"].decode()
for name in ("panicHandler", "xt_unhandled_exception"):
    start = handler.index(f"void IRAM_ATTR {name}(void *frame)\n{{")
    assert handler.index("ouse_fault_capture(frame);", start) < handler.index("panic_enable_cache();", start)
assert arch.index("ouse_fault_print(core);") < arch.index("panic_print_register_array(desc, f, DIM(desc));")
assert arch.index("ouse_fault_print(core);") < arch.index('panic_print_str("Core ");')
# Keep official noflash linker object selectors, per-source flags, and unrelated sources.
commands = json.loads((OUT / "generated/compile_commands.json").read_text())
selected = (OUT / "generated/selected.txt").read_text().split(";")
assert len(selected) == 3
assert all(Path(p).parent == generated for p in selected[:2])
assert [Path(p).name for p in selected] == ["panic_handler.c", "panic_arch.c", "other.c"]
assert "/Od" in next(c["command"] for c in commands if c["file"].endswith("panic_arch.c"))
assert "p4_fault_snapshot.inc" in arch
for broken in (
    originals["panic_handler.c"].replace(b"xt_unhandled_exception(void *frame)", b"changed_entry(void *frame)"),
    originals["panic_handler.c"] + b"\nvoid IRAM_ATTR xt_unhandled_exception(void *frame)\n{\n}\n",
):
    (fixture / "panic_handler.c").write_bytes(broken)
    configure(False)
(fixture / "panic_handler.c").write_bytes(originals["panic_handler.c"])
(OUT / "CMakeLists.txt").write_text(project.replace("set(CONFIG_ESP_PANIC_HANDLER_IRAM y)", "set(CONFIG_ESP_PANIC_HANDLER_IRAM n)"))
configure(False)
(OUT / "CMakeLists.txt").write_text(project)
configure()
(fixture/"core_dump_elf.c").write_bytes(originals["core_dump_elf.c"].replace(b"heap_caps_walk(MALLOC_CAP_8BIT,",b"changed_heap_walker(MALLOC_CAP_8BIT,"))
configure(False)
(fixture/"core_dump_elf.c").write_bytes(originals["core_dump_elf.c"])
configure()
for name, path in paths.items():
    assert hashlib.sha256(path.read_bytes()).hexdigest() == hashes[name]
print("PASS: real CMake preserves panic object names/flags, hooks before cache/MEPC changes, fails on SDK drift")

# Execute actual capture/printer with CSR transport and panic output replaced.
stub = OUT / "stub"
(stub / "riscv").mkdir(parents=True, exist_ok=True)
(stub / "esp_attr.h").write_text("#define IRAM_ATTR\n#define DRAM_ATTR\n#define RTC_NOINIT_ATTR\n#define DRAM_STR(s) (s)\n")
(stub / "riscv/csr.h").write_text("""
#define mhartid 0xF14
#define MEXSTATUS 0x7E1
#define MHINT 0x7C5
#define LDPC0 0xBE0
#define LDPC1 0xBE1
#define STPC0 0xBF0
#define STPC1 0xBF1
#define STPC2 0xBF2
#define CSR_PMPCFG0 0x3A0
#define CSR_PMPADDR0 0x3B0
#define CSR_PMACFG0 0xBC0
#define CSR_PMAADDR0 0xBD0
#define RV_READ_CSR(csr) read_csr(csr)
""")
capture = CAPTURE.read_text().replace('__asm__ __volatile__ ("" ::: "memory");', "/* host compiler barrier omitted */")
# Inject a read-only MMIO transport. Verify every register token against the
# selected rev3 SDK header; the firmware build checks its actual bus address.
(stub / "soc").mkdir(exist_ok=True)
(stub / "soc/soc.h").write_text("#define REG_READ(reg) read_mmio(reg)\n")
registers = re.findall(r"X\(\w+, (\w+_REG)\)", capture)
for header in ("cache_reg.h", "icm_sys_reg.h", "hp_system_reg.h", "assist_debug_reg.h"):
    actual = (SDK / "components/soc/esp32p4/register/hw_ver3/soc" / header).read_text()
    selected_regs = [r for r in registers if re.search(r"^#define " + r + r" ", actual, re.M)]
    (stub / "soc" / header).write_text("\n".join(
        f"#define {r} {registers.index(r)}" for r in selected_regs) + "\n")
assert len(registers) == len(set(registers)) == 34
encoder = uart[uart.index("static void esp_core_dump_b64_encode"):uart.index("static void esp_core_dump_uart_print_write_start")]
writer = uart[uart.index("static esp_err_t esp_core_dump_uart_write_data("):uart.index("static int esp_core_dump_uart_get_char")]
dump_body = r'''
/* Unmodified IDF UART writer/encoder uses 32-bit sizes; host pointers are 64-bit. */
#pragma warning(push)
#pragma warning(disable: 4018 4244 4267)
typedef int esp_err_t;
typedef struct { uint32_t off; int checksum_ctx; } core_dump_write_data_t;
#define ESP_OK 0
#define ESP_COREDUMP_ASSERT(value) assert(value)
#define ESP_COREDUMP_PRINT(fmt, payload) dump_print(fmt, payload)
static unsigned feeds, checksum_bytes;
static void esp_core_dump_checksum_update(int *ctx, const void *data, uint32_t size) {
    (void)ctx; (void)data; checksum_bytes += size;
}
static void dump_print(const char *format, const char *payload) {
    (void)format; strcat_s(output, sizeof(output), payload); strcat_s(output, sizeof(output), "|");
}
/* Compile the generated declaration before the writer and define the test
 * transport afterwards. A missing declaration must fail, as in a real C TU. */
'''+wdt_declaration+'\n'+encoder+writer+'''
void esp_panic_handler_feed_wdts(void) { ++feeds; }
#pragma warning(pop)
'''
source = r'''
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#define CONFIG_IDF_TARGET_ESP32P4 1
#define CONFIG_ESP32P4_REV_MIN_FULL 301
#define CONFIG_ESP_PANIC_HANDLER_IRAM 1
#define SOC_ASYNCHRONOUS_BUS_ERROR_MODE 1
#define SOC_CPU_CORES_NUM 2
typedef struct { uint32_t mepc, mcause, mtval, mstatus, sp, ra; } RvExcFrame;
static uint32_t csr[4096];
static unsigned reads;
static unsigned mmio_reads;
static uint32_t read_mmio(unsigned address) {
    ++mmio_reads;
    return 0x90000000u + address;
}
static int nest;
static RvExcFrame nested;
void ouse_fault_capture(const void *);
static uint32_t read_csr(unsigned address) {
    ++reads;
    if (nest && address == 0x7E1) { nest = 0; ouse_fault_capture(&nested); }
    return csr[address];
}
static char output[20000];
static void panic_print_str(const char *s) { strcat_s(output, sizeof(output), s); }
static void panic_print_hex(int n) { char b[32]; sprintf_s(b, sizeof(b), "%08x", (unsigned)n); strcat_s(output, sizeof(output), b); }
static void panic_print_dec(int n) { char b[32]; sprintf_s(b, sizeof(b), "%d", n); strcat_s(output, sizeof(output), b); }
CAPTURE_BODY
DUMP_BODY
int main(void) {
    _Static_assert(sizeof(s_ouse_fault) <= 1024, "snapshot should remain small internal DRAM");
    for (unsigned i = 0; i < 4096; ++i) csr[i] = 0x60000000u + i;
    csr[mhartid] = 1;
    csr[MEXSTATUS] = 1u << 8;
    csr[LDPC0] = 0x48001001; csr[0xBE8] = 0x48780000;
    csr[LDPC1] = 0x48002000; csr[0xBE9] = 0x4ff65000; /* invalid history entry */
    csr[STPC2] = 0x48003001; csr[0xBFA] = 0x4ff66000;
    RvExcFrame frame = {0x48009900, 5, 0x4ff77000, 0x11881, 0x4ff88000, 0x48008800};
    nest = 1;
    ouse_fault_capture(&frame);
    assert(output[0] == 0); /* Capture never prints. */
    assert(s_ouse_fault[1].state == 2 && s_ouse_fault[0].state == 0);
    assert(s_ouse_fault[1].raw_mepc == frame.mepc && frame.mepc == 0x48009900);
    assert(s_ouse_fault[1].ldtval[0] == 0x48780000); /* actual target differs from MTVAL */
    assert(s_ouse_fault[1].pmpaddr[31] == csr[CSR_PMPADDR0+31]);
    assert(s_ouse_fault[1].pmaaddr[15] == csr[CSR_PMAADDR0+15]);
    assert(mmio_reads == MMIO_REGISTER_COUNT);
    assert(s_ouse_fault[1].bus.core0_rcd_en == 0x90000000u + ASSIST_DEBUG_CORE_0_RCD_EN_REG);
    assert(s_ouse_fault[1].bus.core0_pdebugpc == 0x90000000u + ASSIST_DEBUG_CORE_0_RCD_PDEBUGPC_REG);
    assert(s_ouse_fault[1].bus.core0_pdebugsp == 0x90000000u + ASSIST_DEBUG_CORE_0_RCD_PDEBUGSP_REG);
    assert(s_ouse_fault[1].bus.core0_lastpc == 0x90000000u + ASSIST_DEBUG_CORE_0_LASTPC_BEFORE_EXCEPTION_REG);
    assert(s_ouse_fault[1].bus.core1_rcd_en == 0x90000000u + ASSIST_DEBUG_CORE_1_RCD_EN_REG);
    assert(s_ouse_fault[1].bus.core1_pdebugpc == 0x90000000u + ASSIST_DEBUG_CORE_1_RCD_PDEBUGPC_REG);
    assert(s_ouse_fault[1].bus.core1_pdebugsp == 0x90000000u + ASSIST_DEBUG_CORE_1_RCD_PDEBUGSP_REG);
    assert(s_ouse_fault[1].bus.core1_lastpc == 0x90000000u + ASSIST_DEBUG_CORE_1_LASTPC_BEFORE_EXCEPTION_REG);
    assert(s_ouse_fault[1].bus.cache_sync_ctrl == 0x90000000u + CACHE_SYNC_CTRL_REG);
    assert(s_ouse_fault[1].bus.cache_sync_map == 0x90000000u + CACHE_SYNC_MAP_REG);
    assert(s_ouse_fault[1].bus.cache_sync_addr == 0x90000000u + CACHE_SYNC_ADDR_REG);
    assert(s_ouse_fault[1].bus.cache_sync_size == 0x90000000u + CACHE_SYNC_SIZE_REG);
    assert(s_ouse_fault[1].bus.l1_fail == 0x90000000u + CACHE_L1_CACHE_ACS_FAIL_INT_RAW_REG);
    assert(s_ouse_fault[1].bus.core_timeout_raw == 0x90000000u + HP_SYSTEM_CORE_TIMEOUT_INT_RAW_REG);
    frame.mepc = 0x48001000; /* IDF later changes only MEPC. */
    unsigned saved_reads = reads;
    ouse_fault_capture(&frame);
    assert(reads == saved_reads+1); /* only hartid; first snapshot preserved */
    assert(mmio_reads == MMIO_REGISTER_COUNT); /* no second MMIO read after nested/repeated panic */
    assert(s_ouse_fault[1].raw_mepc == 0x48009900);
    ouse_fault_print(1);
    assert(strstr(output, "RAW_MEPC=0x48009900"));
    assert(strstr(output, "BUS_ERR=0x00000001 PMP_ERR=0x00000000"));
    assert(strstr(output, "PC=0x48001000 VALID=0x00000001 ADDR=0x48780000"));
    assert(strstr(output, "PC=0x48002000 VALID=0x00000000 ADDR=0x4ff65000"));
    assert(strstr(output, "STORE2") && strstr(output, "ADDR=0x4ff66000"));
    assert(strstr(output, "[OuseFault v2]"));
    assert(strstr(output, "HW_l1_fail=MMIO_L1_FAIL_VALUE"));
    assert(strstr(output, "HW_core_timeout_raw=MMIO_CORE_TIMEOUT_VALUE"));
    assert(s_ouse_retained[1].magic==OUSE_RETAINED_MAGIC);
    assert(s_ouse_retained[1].mepc==0x48009900);
    volatile unsigned char *bss=(volatile unsigned char *)s_ouse_fault;
    for(size_t i=0;i<sizeof(s_ouse_fault);++i)bss[i]=0; // startup clears BSS, not RTC no-init
    output[0]=0;ouse_fault_report_previous();
    assert(strstr(output,"[OuseFault retained v3] CORE=0x00000001 RAW_MEPC=0x48009900"));
    assert(strstr(output,"PREV_LOAD0") && strstr(output,"ADDR=0x48780000"));
    output[0]=0;ouse_fault_report_previous();assert(output[0]==0);
    s_ouse_retained[0].magic=OUSE_RETAINED_MAGIC;s_ouse_retained[0].checksum=0;
    output[0]=0;ouse_fault_report_previous();assert(output[0]==0); // reject incomplete/corrupt retained state
    csr[mhartid] = 0; csr[MEXSTATUS] = 1u << 9;
    ouse_fault_capture(&frame);
    output[0] = 0; ouse_fault_print(0);
    assert(strstr(output, "BUS_ERR=0x00000000 PMP_ERR=0x00000001"));
    csr[mhartid] = 8; ouse_fault_capture(&frame);
    ouse_fault_capture(NULL); /* invalid core/frame do not index memory */
    output[0] = 0; ouse_fault_print(-1); ouse_fault_print(8);
    assert(output[0] == 0);
    s_ouse_fault[0].state = 1; ouse_fault_print(0);
    assert(strstr(output, "Snapshot absent/incomplete"));
    output[0] = 0;
    uint8_t data[97] = {0};
    core_dump_write_data_t wr = {0};
    assert(esp_core_dump_uart_write_data(&wr, data, sizeof(data)) == ESP_OK);
    assert(feeds == 3 && checksum_bytes == 97 && wr.off == 97);
    assert(strlen(output) == 135 && output[64] == '|' && output[129] == '|');
    assert(strcmp(output+130, "AA==|") == 0);
    for (unsigned i = 0; i < 129; ++i) assert(output[i] == (i == 64 ? '|' : 'A'));
    puts("PASS: actual capture/printer preserves first per-core snapshot, handles nesting, raw PC and valid history addresses");
    puts("PASS: actual UART writer feeds WDT per progressing chunk; base64/checksum byte count unchanged");
    return 0;
}
'''.replace("CAPTURE_BODY", capture).replace("DUMP_BODY", dump_body).replace(
    "MMIO_REGISTER_COUNT", str(len(registers))).replace(
    "MMIO_L1_FAIL_VALUE", f"0x{0x90000000 + registers.index('CACHE_L1_CACHE_ACS_FAIL_INT_RAW_REG'):08x}").replace(
    "MMIO_CORE_TIMEOUT_VALUE", f"0x{0x90000000 + registers.index('HP_SYSTEM_CORE_TIMEOUT_INT_RAW_REG'):08x}")
(OUT / "check.c").write_text(source)
p = subprocess.run([str(cc), "/nologo", "/W4", "/WX", "/std:c11",
    "/I" + str(stub), str(OUT / "check.c"), "/Fe" + str(OUT / "check.exe")],
    cwd=OUT, env=env, capture_output=True, text=True, errors="replace")
assert p.returncode == 0, (p.stdout, p.stderr)
subprocess.run([str(OUT / "check.exe")], check=True)
missing_declaration = OUT / "missing-declaration.c"
missing_declaration.write_text(source.replace(wdt_declaration, "", 1))
negative = subprocess.run([str(cc), "/nologo", "/W4", "/WX", "/std:c11", "/c",
    "/I" + str(stub), str(missing_declaration)], cwd=OUT, env=env,
    capture_output=True, text=True, errors="replace")
assert negative.returncode != 0 and "C4013" in negative.stdout, (negative.returncode, negative.stdout, negative.stderr)
print("PASS: removing the generated WDT declaration reproduces a compile failure")
print("PASS: ESP-IDF installation unchanged; no ESP firmware compilation or device access")
