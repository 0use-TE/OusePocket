/*
 * Product-only ESP32-P4 v3 panic diagnostics, included by generated panic_arch.c.
 * No heap, locks, RTOS calls, CSR writes, or printing in the early capture path.
 * Keep this in esp_system's panic_arch object: linker.lf maps it to IRAM/DRAM.
 */
#include "esp_attr.h"
#include "riscv/csr.h"
#include "soc/soc.h"
#include "soc/cache_reg.h"
#include "soc/icm_sys_reg.h"
#include "soc/hp_system_reg.h"
#include "soc/assist_debug_reg.h"

/* Read status and configuration only. Never clear flags, enable clocks, or
 * suppress error responses: the first fault must retain its original cause.
 * Addresses/attributes are meaningful only with the matching raw error bit.
 */
#define OUSE_FAULT_BUS_REGS(X) \
    X(core0_rcd_en, ASSIST_DEBUG_CORE_0_RCD_EN_REG) \
    X(core0_pdebugpc, ASSIST_DEBUG_CORE_0_RCD_PDEBUGPC_REG) \
    X(core0_pdebugsp, ASSIST_DEBUG_CORE_0_RCD_PDEBUGSP_REG) \
    X(core0_lastpc, ASSIST_DEBUG_CORE_0_LASTPC_BEFORE_EXCEPTION_REG) \
    X(core1_rcd_en, ASSIST_DEBUG_CORE_1_RCD_EN_REG) \
    X(core1_pdebugpc, ASSIST_DEBUG_CORE_1_RCD_PDEBUGPC_REG) \
    X(core1_pdebugsp, ASSIST_DEBUG_CORE_1_RCD_PDEBUGSP_REG) \
    X(core1_lastpc, ASSIST_DEBUG_CORE_1_LASTPC_BEFORE_EXCEPTION_REG) \
    X(cache_sync_ctrl, CACHE_SYNC_CTRL_REG) \
    X(cache_sync_map, CACHE_SYNC_MAP_REG) \
    X(cache_sync_addr, CACHE_SYNC_ADDR_REG) \
    X(cache_sync_size, CACHE_SYNC_SIZE_REG) \
    X(l1_fail, CACHE_L1_CACHE_ACS_FAIL_INT_RAW_REG) \
    X(l1_data_attr, CACHE_L1_DCACHE_ACS_FAIL_ID_ATTR_REG) \
    X(l1_data_addr, CACHE_L1_DCACHE_ACS_FAIL_ADDR_REG) \
    X(l2_fail, CACHE_L2_CACHE_ACS_FAIL_INT_RAW_REG) \
    X(l2_attr, CACHE_L2_CACHE_ACS_FAIL_ID_ATTR_REG) \
    X(l2_addr, CACHE_L2_CACHE_ACS_FAIL_ADDR_REG) \
    X(l1_data_ctrl, CACHE_L1_DCACHE_CTRL_REG) \
    X(l2_ctrl, CACHE_L2_CACHE_CTRL_REG) \
    X(icm_raw, ICM_INT_RAW_REG) \
    X(icm_deadlock, ICM_DLOCK_STATUS_REG) \
    X(icm_cpu_addr, ICM_CPU_ADDRHOLE_ADDR_REG) \
    X(icm_cpu_info, ICM_CPU_ADDRHOLE_INFO_REG) \
    X(icm_sys_addr, ICM_SYS_ADDRHOLE_ADDR_REG) \
    X(icm_sys_info, ICM_SYS_ADDRHOLE_INFO_REG) \
    X(sram_raw, HP_SYSTEM_L2_MEM_INT_RAW_REG) \
    X(sram_addr_record, HP_SYSTEM_L2_MEM_INT_RECORD0_REG) \
    X(sram_ecc_record, HP_SYSTEM_L2_MEM_INT_RECORD1_REG) \
    X(sram_ecc_enable, HP_SYSTEM_L2_MEM_L2_RAM_ECC_REG) \
    X(sram_error_enable, HP_SYSTEM_L2_MEM_ERR_RESP_CTRL_REG) \
    X(ahb_error_raw, HP_SYSTEM_AHB2AXI_BRESP_ERR_INT_RAW_REG) \
    X(core_error_disable, HP_SYSTEM_CORE_ERR_RESP_DIS_REG) \
    X(core_timeout_raw, HP_SYSTEM_CORE_TIMEOUT_INT_RAW_REG)

typedef struct {
#define OUSE_FAULT_BUS_FIELD(name, reg) uint32_t name;
    OUSE_FAULT_BUS_REGS(OUSE_FAULT_BUS_FIELD)
#undef OUSE_FAULT_BUS_FIELD
} ouse_fault_bus_t;

#if !CONFIG_IDF_TARGET_ESP32P4 || CONFIG_ESP32P4_REV_MIN_FULL < 300
#error "Ouse fault snapshot requires ESP32-P4 v3 PMP/CSR layout"
#endif
#if !SOC_ASYNCHRONOUS_BUS_ERROR_MODE || !CONFIG_ESP_PANIC_HANDLER_IRAM
#error "Ouse fault snapshot requires asynchronous bus CSR support and IRAM panic handling"
#endif

typedef struct {
    uint32_t state; /* 0 empty, 1 capturing, 2 complete; never overwrite first entry */
    uint32_t raw_mepc, mcause, mtval, mstatus, sp, ra;
    uint32_t mexstatus, mhint;
    uint32_t ldpc[2], ldtval[2], stpc[3], sttval[3];
    uint32_t pmpcfg[8], pmpaddr[32], pmacfg[16], pmaaddr[16];
    ouse_fault_bus_t bus;
} ouse_fault_snapshot_t;

static DRAM_ATTR volatile ouse_fault_snapshot_t s_ouse_fault[SOC_CPU_CORES_NUM];

// Timer-group WDT reset can interrupt a dual-core panic before UART printing.
// LP/RTC no-init RAM survives that reset. Commit a small record BEFORE MMIO;
// never clear/disable watchdogs or change the panic arbitration to collect it.
typedef struct {
    uint32_t magic,checksum;
    uint32_t mepc,mcause,mtval,sp,ra,mexstatus;
    uint32_t ldpc[2],ldtval[2],stpc[3],sttval[3];
} ouse_retained_fault_t;
static RTC_NOINIT_ATTR volatile ouse_retained_fault_t s_ouse_retained[SOC_CPU_CORES_NUM];
#define OUSE_RETAINED_MAGIC 0x4f465433u
static uint32_t IRAM_ATTR ouse_retained_checksum(const volatile ouse_retained_fault_t *r)
{
    const volatile uint32_t *words=&r->mepc;
    uint32_t checksum=0x9e3779b9u;
    for(unsigned i=0;i<(sizeof(*r)-2*sizeof(uint32_t))/sizeof(uint32_t);++i)
        checksum=(checksum<<5 | checksum>>27)^words[i];
    return checksum;
}
static void IRAM_ATTR ouse_fault_retain(unsigned core,const volatile ouse_fault_snapshot_t *s)
{
    volatile ouse_retained_fault_t *r=&s_ouse_retained[core];r->magic=0;
    r->mepc=s->raw_mepc;r->mcause=s->mcause;r->mtval=s->mtval;
    r->sp=s->sp;r->ra=s->ra;r->mexstatus=s->mexstatus;
    for(unsigned i=0;i<2;++i){r->ldpc[i]=s->ldpc[i];r->ldtval[i]=s->ldtval[i];}
    for(unsigned i=0;i<3;++i){r->stpc[i]=s->stpc[i];r->sttval[i]=s->sttval[i];}
    r->checksum=ouse_retained_checksum(r);
    __asm__ __volatile__ ("" ::: "memory");
    r->magic=OUSE_RETAINED_MAGIC;
}

void IRAM_ATTR ouse_fault_capture(const void *f)
{
    const uint32_t core = RV_READ_CSR(mhartid);
    if (core >= SOC_CPU_CORES_NUM || f == NULL) return;
    volatile ouse_fault_snapshot_t *s = &s_ouse_fault[core];
    if (s->state != 0) return;
    s->state = 1;
    __asm__ __volatile__ ("" ::: "memory");
    /* Snapshot volatile status/history before inspecting the supplied frame. */
    s->mexstatus = RV_READ_CSR(MEXSTATUS);
    s->mhint = RV_READ_CSR(MHINT);
    s->ldpc[0] = RV_READ_CSR(LDPC0);
    s->ldpc[1] = RV_READ_CSR(LDPC1);
    /* P4 TRM Bus Error Exception Context CSRs: these names are absent in csr.h. */
    s->ldtval[0] = RV_READ_CSR(0xBE8);
    s->ldtval[1] = RV_READ_CSR(0xBE9);
    s->stpc[0] = RV_READ_CSR(STPC0);
    s->stpc[1] = RV_READ_CSR(STPC1);
    s->stpc[2] = RV_READ_CSR(STPC2);
    s->sttval[0] = RV_READ_CSR(0xBF8);
    s->sttval[1] = RV_READ_CSR(0xBF9);
    s->sttval[2] = RV_READ_CSR(0xBFA);
    const RvExcFrame *frame = (const RvExcFrame *)f;
    s->raw_mepc = frame->mepc;
    s->mcause = frame->mcause;
    s->mtval = frame->mtval;
    s->mstatus = frame->mstatus;
    s->sp = frame->sp;
    s->ra = frame->ra;
    ouse_fault_retain(core,s);
    /* Capture before panic_enable_cache() can change the cache configuration. */
    /* Both cores' debug records come first, before slower error/config reads.
     * PDEBUG values are useful only when the matching RCD_EN enables recording;
     * they sample execution during capture, not an atomic first-fault context.
     * In particular a peer task's saved TCB PC need not describe its active ISR.
     */
#define OUSE_FAULT_BUS_READ(name, reg) s->bus.name = REG_READ(reg);
    OUSE_FAULT_BUS_REGS(OUSE_FAULT_BUS_READ)
#undef OUSE_FAULT_BUS_READ
    /* CSR numbers must be compile-time immediates. Read only implemented v3 entries. */
    s->pmpcfg[0] = RV_READ_CSR(CSR_PMPCFG0 + 0);
    s->pmpcfg[1] = RV_READ_CSR(CSR_PMPCFG0 + 1);
    s->pmpcfg[2] = RV_READ_CSR(CSR_PMPCFG0 + 2);
    s->pmpcfg[3] = RV_READ_CSR(CSR_PMPCFG0 + 3);
    s->pmpcfg[4] = RV_READ_CSR(CSR_PMPCFG0 + 4);
    s->pmpcfg[5] = RV_READ_CSR(CSR_PMPCFG0 + 5);
    s->pmpcfg[6] = RV_READ_CSR(CSR_PMPCFG0 + 6);
    s->pmpcfg[7] = RV_READ_CSR(CSR_PMPCFG0 + 7);
    s->pmpaddr[0] = RV_READ_CSR(CSR_PMPADDR0 + 0);
    s->pmpaddr[1] = RV_READ_CSR(CSR_PMPADDR0 + 1);
    s->pmpaddr[2] = RV_READ_CSR(CSR_PMPADDR0 + 2);
    s->pmpaddr[3] = RV_READ_CSR(CSR_PMPADDR0 + 3);
    s->pmpaddr[4] = RV_READ_CSR(CSR_PMPADDR0 + 4);
    s->pmpaddr[5] = RV_READ_CSR(CSR_PMPADDR0 + 5);
    s->pmpaddr[6] = RV_READ_CSR(CSR_PMPADDR0 + 6);
    s->pmpaddr[7] = RV_READ_CSR(CSR_PMPADDR0 + 7);
    s->pmpaddr[8] = RV_READ_CSR(CSR_PMPADDR0 + 8);
    s->pmpaddr[9] = RV_READ_CSR(CSR_PMPADDR0 + 9);
    s->pmpaddr[10] = RV_READ_CSR(CSR_PMPADDR0 + 10);
    s->pmpaddr[11] = RV_READ_CSR(CSR_PMPADDR0 + 11);
    s->pmpaddr[12] = RV_READ_CSR(CSR_PMPADDR0 + 12);
    s->pmpaddr[13] = RV_READ_CSR(CSR_PMPADDR0 + 13);
    s->pmpaddr[14] = RV_READ_CSR(CSR_PMPADDR0 + 14);
    s->pmpaddr[15] = RV_READ_CSR(CSR_PMPADDR0 + 15);
    s->pmpaddr[16] = RV_READ_CSR(CSR_PMPADDR0 + 16);
    s->pmpaddr[17] = RV_READ_CSR(CSR_PMPADDR0 + 17);
    s->pmpaddr[18] = RV_READ_CSR(CSR_PMPADDR0 + 18);
    s->pmpaddr[19] = RV_READ_CSR(CSR_PMPADDR0 + 19);
    s->pmpaddr[20] = RV_READ_CSR(CSR_PMPADDR0 + 20);
    s->pmpaddr[21] = RV_READ_CSR(CSR_PMPADDR0 + 21);
    s->pmpaddr[22] = RV_READ_CSR(CSR_PMPADDR0 + 22);
    s->pmpaddr[23] = RV_READ_CSR(CSR_PMPADDR0 + 23);
    s->pmpaddr[24] = RV_READ_CSR(CSR_PMPADDR0 + 24);
    s->pmpaddr[25] = RV_READ_CSR(CSR_PMPADDR0 + 25);
    s->pmpaddr[26] = RV_READ_CSR(CSR_PMPADDR0 + 26);
    s->pmpaddr[27] = RV_READ_CSR(CSR_PMPADDR0 + 27);
    s->pmpaddr[28] = RV_READ_CSR(CSR_PMPADDR0 + 28);
    s->pmpaddr[29] = RV_READ_CSR(CSR_PMPADDR0 + 29);
    s->pmpaddr[30] = RV_READ_CSR(CSR_PMPADDR0 + 30);
    s->pmpaddr[31] = RV_READ_CSR(CSR_PMPADDR0 + 31);
    s->pmacfg[0] = RV_READ_CSR(CSR_PMACFG0 + 0);
    s->pmaaddr[0] = RV_READ_CSR(CSR_PMAADDR0 + 0);
    s->pmacfg[1] = RV_READ_CSR(CSR_PMACFG0 + 1);
    s->pmaaddr[1] = RV_READ_CSR(CSR_PMAADDR0 + 1);
    s->pmacfg[2] = RV_READ_CSR(CSR_PMACFG0 + 2);
    s->pmaaddr[2] = RV_READ_CSR(CSR_PMAADDR0 + 2);
    s->pmacfg[3] = RV_READ_CSR(CSR_PMACFG0 + 3);
    s->pmaaddr[3] = RV_READ_CSR(CSR_PMAADDR0 + 3);
    s->pmacfg[4] = RV_READ_CSR(CSR_PMACFG0 + 4);
    s->pmaaddr[4] = RV_READ_CSR(CSR_PMAADDR0 + 4);
    s->pmacfg[5] = RV_READ_CSR(CSR_PMACFG0 + 5);
    s->pmaaddr[5] = RV_READ_CSR(CSR_PMAADDR0 + 5);
    s->pmacfg[6] = RV_READ_CSR(CSR_PMACFG0 + 6);
    s->pmaaddr[6] = RV_READ_CSR(CSR_PMAADDR0 + 6);
    s->pmacfg[7] = RV_READ_CSR(CSR_PMACFG0 + 7);
    s->pmaaddr[7] = RV_READ_CSR(CSR_PMAADDR0 + 7);
    s->pmacfg[8] = RV_READ_CSR(CSR_PMACFG0 + 8);
    s->pmaaddr[8] = RV_READ_CSR(CSR_PMAADDR0 + 8);
    s->pmacfg[9] = RV_READ_CSR(CSR_PMACFG0 + 9);
    s->pmaaddr[9] = RV_READ_CSR(CSR_PMAADDR0 + 9);
    s->pmacfg[10] = RV_READ_CSR(CSR_PMACFG0 + 10);
    s->pmaaddr[10] = RV_READ_CSR(CSR_PMAADDR0 + 10);
    s->pmacfg[11] = RV_READ_CSR(CSR_PMACFG0 + 11);
    s->pmaaddr[11] = RV_READ_CSR(CSR_PMAADDR0 + 11);
    s->pmacfg[12] = RV_READ_CSR(CSR_PMACFG0 + 12);
    s->pmaaddr[12] = RV_READ_CSR(CSR_PMAADDR0 + 12);
    s->pmacfg[13] = RV_READ_CSR(CSR_PMACFG0 + 13);
    s->pmaaddr[13] = RV_READ_CSR(CSR_PMAADDR0 + 13);
    s->pmacfg[14] = RV_READ_CSR(CSR_PMACFG0 + 14);
    s->pmaaddr[14] = RV_READ_CSR(CSR_PMAADDR0 + 14);
    s->pmacfg[15] = RV_READ_CSR(CSR_PMACFG0 + 15);
    s->pmaaddr[15] = RV_READ_CSR(CSR_PMAADDR0 + 15);
    __asm__ __volatile__ ("" ::: "memory");
    s->state = 2;
}

static void IRAM_ATTR ouse_fault_hex(const char *name, uint32_t value)
{
    panic_print_str(name);
    panic_print_str(DRAM_STR("=0x"));
    panic_print_hex(value);
    panic_print_str(DRAM_STR(" "));
}

static void IRAM_ATTR ouse_fault_history(const char *name, unsigned index,
                                        uint32_t pc, uint32_t address)
{
    panic_print_str(name);
    panic_print_dec(index);
    panic_print_str(DRAM_STR(" "));
    ouse_fault_hex(DRAM_STR("RAW_PC"), pc);
    ouse_fault_hex(DRAM_STR("PC"), pc & ~1U);
    ouse_fault_hex(DRAM_STR("VALID"), pc & 1U);
    ouse_fault_hex(DRAM_STR("ADDR"), address);
    panic_print_str(DRAM_STR("\r\n"));
}

// Called once at app_main entry, before starting display or any worker.
void ouse_fault_report_previous(void)
{
    for(unsigned core=0;core<SOC_CPU_CORES_NUM;++core){
        volatile ouse_retained_fault_t *r=&s_ouse_retained[core];
        if(r->magic==OUSE_RETAINED_MAGIC && r->checksum==ouse_retained_checksum(r)){
            panic_print_str(DRAM_STR("\r\n[OuseFault retained v3] "));
            ouse_fault_hex(DRAM_STR("CORE"),core);
            ouse_fault_hex(DRAM_STR("RAW_MEPC"),r->mepc);
            ouse_fault_hex(DRAM_STR("MCAUSE"),r->mcause);
            ouse_fault_hex(DRAM_STR("MTVAL"),r->mtval);
            ouse_fault_hex(DRAM_STR("SP"),r->sp);
            ouse_fault_hex(DRAM_STR("RA"),r->ra);
            ouse_fault_hex(DRAM_STR("MEXSTATUS"),r->mexstatus);
            panic_print_str(DRAM_STR("\r\n"));
            for(unsigned i=0;i<2;++i)ouse_fault_history(DRAM_STR("PREV_LOAD"),i,r->ldpc[i],r->ldtval[i]);
            for(unsigned i=0;i<3;++i)ouse_fault_history(DRAM_STR("PREV_STORE"),i,r->stpc[i],r->sttval[i]);
        }
        r->magic=0; // a normal reboot must not report this fault repeatedly
    }
}

static void IRAM_ATTR ouse_fault_print(int core)
{
    if (core < 0 || core >= SOC_CPU_CORES_NUM) return;
    const volatile ouse_fault_snapshot_t *s = &s_ouse_fault[core];
    panic_print_str(DRAM_STR("\r\n[OuseFault v2] "));
    ouse_fault_hex(DRAM_STR("CORE"), core);
    ouse_fault_hex(DRAM_STR("STATE"), s->state);
    panic_print_str(DRAM_STR("\r\n"));
    if (s->state != 2) {
        panic_print_str(DRAM_STR("Snapshot absent/incomplete; do not infer fault cause.\r\n"));
        return;
    }
    ouse_fault_hex(DRAM_STR("RAW_MEPC"), s->raw_mepc);
    ouse_fault_hex(DRAM_STR("MCAUSE"), s->mcause);
    ouse_fault_hex(DRAM_STR("MTVAL"), s->mtval);
    ouse_fault_hex(DRAM_STR("MSTATUS"), s->mstatus);
    panic_print_str(DRAM_STR("\r\n"));
    ouse_fault_hex(DRAM_STR("SP"), s->sp);
    ouse_fault_hex(DRAM_STR("RA"), s->ra);
    ouse_fault_hex(DRAM_STR("MEXSTATUS"), s->mexstatus);
    ouse_fault_hex(DRAM_STR("MHINT"), s->mhint);
    panic_print_str(DRAM_STR("\r\n"));
    ouse_fault_hex(DRAM_STR("BUS_ERR"), (s->mexstatus >> 8) & 1);
    ouse_fault_hex(DRAM_STR("PMP_ERR"), (s->mexstatus >> 9) & 1);
    ouse_fault_hex(DRAM_STR("FETCH_ERR"), (s->mexstatus >> 12) & 1);
    panic_print_str(DRAM_STR("\r\n"));
    for (unsigned i = 0; i < 2; ++i)
        ouse_fault_history(DRAM_STR("LOAD"), i, s->ldpc[i], s->ldtval[i]);
    for (unsigned i = 0; i < 3; ++i)
        ouse_fault_history(DRAM_STR("STORE"), i, s->stpc[i], s->sttval[i]);
    panic_print_str(DRAM_STR("HW flags are raw; use addresses only when corresponding flag is set.\r\n"));
#define OUSE_FAULT_BUS_PRINT(name, reg) \
    ouse_fault_hex(DRAM_STR("HW_" #name), s->bus.name); \
    panic_print_str(DRAM_STR("\r\n"));
    OUSE_FAULT_BUS_REGS(OUSE_FAULT_BUS_PRINT)
#undef OUSE_FAULT_BUS_PRINT
    panic_print_str(DRAM_STR("History: use VALID=1 with BUS_ERR=1; index 0 is newest.\r\n"));
    for (unsigned i = 0; i < 8; ++i) {
        panic_print_str(DRAM_STR("PMP_CFG "));
        panic_print_dec(i);
        panic_print_str(DRAM_STR(" "));
        ouse_fault_hex(DRAM_STR("RAW"), s->pmpcfg[i]);
        panic_print_str(DRAM_STR("\r\n"));
    }
    for (unsigned i = 0; i < 32; ++i) {
        panic_print_str(DRAM_STR("PMP_ADDR "));
        panic_print_dec(i);
        panic_print_str(DRAM_STR(" "));
        ouse_fault_hex(DRAM_STR("RAW"), s->pmpaddr[i]);
        panic_print_str(DRAM_STR("\r\n"));
    }
    for (unsigned i = 0; i < 16; ++i) {
        panic_print_str(DRAM_STR("PMA "));
        panic_print_dec(i);
        panic_print_str(DRAM_STR(" "));
        ouse_fault_hex(DRAM_STR("CFG_RAW"), s->pmacfg[i]);
        ouse_fault_hex(DRAM_STR("ADDR_RAW"), s->pmaaddr[i]);
        panic_print_str(DRAM_STR("\r\n"));
    }
    panic_print_str(DRAM_STR("[OuseFault end]\r\n"));
}
