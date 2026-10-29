/* Fexox OS Project, EULA GPLv2 */
 

#include <stdint.h>
#include <stddef.h>
#include "debug_out.h"
#include "AcpiControl.h"

extern void *memcpy(void *dst, const void *src, size_t n);

 

#define DIRECT_MAP_OFFSET 0xFFFF880000000000ULL
static inline void *phys_to_kvirt(uint64_t pa) {
    return (void *)(uintptr_t)(pa + DIRECT_MAP_OFFSET);
}

 
static inline void acpi_outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint8_t acpi_inb(uint16_t port) {
    uint8_t r;
    __asm__ volatile ("inb %1, %0" : "=a"(r) : "Nd"(port));
    return r;
}
static inline void acpi_outw(uint16_t port, uint16_t val) {
    __asm__ volatile ("outw %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint16_t acpi_inw(uint16_t port) {
    uint16_t r;
    __asm__ volatile ("inw %1, %0" : "=a"(r) : "Nd"(port));
    return r;
}
static inline void acpi_io_wait(void) {
    __asm__ volatile ("outb %%al, $0x80" : : "a"((uint8_t)0));
}

 



typedef struct __attribute__((packed)) {
    char     signature[8];       
    uint8_t  checksum;
    char     oem_id[6];
    uint8_t  revision;           
    uint32_t rsdt_address;
     
    uint32_t length;
    uint64_t xsdt_address;
    uint8_t  extended_checksum;
    uint8_t  reserved[3];
} acpi_rsdp_t;

typedef struct __attribute__((packed)) {
    char     signature[4];
    uint32_t length;
    uint8_t  revision;
    uint8_t  checksum;
    char     oem_id[6];
    char     oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
} acpi_sdt_header_t;

typedef struct __attribute__((packed)) {
    uint8_t  address_space_id;    
    uint8_t  register_bit_width;
    uint8_t  register_bit_offset;
    uint8_t  access_size;
    uint64_t address;
} acpi_gas_t;

 


typedef struct __attribute__((packed)) {
    acpi_sdt_header_t header;
    uint32_t firmware_ctrl;
    uint32_t dsdt;
    uint8_t  reserved1;
    uint8_t  preferred_pm_profile;
    uint16_t sci_int;
    uint32_t smi_cmd;
    uint8_t  acpi_enable;
    uint8_t  acpi_disable;
    uint8_t  s4bios_req;
    uint8_t  pstate_cnt;
    uint32_t pm1a_evt_blk;
    uint32_t pm1b_evt_blk;
    uint32_t pm1a_cnt_blk;
    uint32_t pm1b_cnt_blk;
    uint32_t pm2_cnt_blk;
    uint32_t pm_tmr_blk;
    uint32_t gpe0_blk;
    uint32_t gpe1_blk;
    uint8_t  pm1_evt_len;
    uint8_t  pm1_cnt_len;
    uint8_t  pm2_cnt_len;
    uint8_t  pm_tmr_len;
    uint8_t  gpe0_blk_len;
    uint8_t  gpe1_blk_len;
    uint8_t  gpe1_base;
    uint8_t  cst_cnt;
    uint16_t p_lvl2_lat;
    uint16_t p_lvl3_lat;
    uint16_t flush_size;
    uint16_t flush_stride;
    uint8_t  duty_offset;
    uint8_t  duty_width;
    uint8_t  day_alrm;
    uint8_t  mon_alrm;
    uint8_t  century;
    uint16_t iapc_boot_arch;
    uint8_t  reserved2;
    uint32_t flags;
    acpi_gas_t reset_reg;
    uint8_t  reset_value;
    uint8_t  reserved3[3];
     
    uint64_t x_firmware_ctrl;
    uint64_t x_dsdt;
    acpi_gas_t x_pm1a_evt_blk;
    acpi_gas_t x_pm1b_evt_blk;
    acpi_gas_t x_pm1a_cnt_blk;
    acpi_gas_t x_pm1b_cnt_blk;
    acpi_gas_t x_pm2_cnt_blk;
    acpi_gas_t x_pm_tmr_blk;
    acpi_gas_t x_gpe0_blk;
    acpi_gas_t x_gpe1_blk;
} acpi_fadt_t;

#define FADT_FLAG_RESET_REG_SUP  (1u << 10)
#define FADT_MIN_LEN_RESET_REG   129    
#define FADT_MIN_LEN_X_DSDT      148    

 


static int      g_fadt_ok         = 0;
static uint32_t g_smi_cmd         = 0;
static uint8_t  g_acpi_enable_val = 0;
static uint32_t g_pm1a_cnt_blk    = 0;
static uint32_t g_pm1b_cnt_blk    = 0;

static int      g_s5_found        = 0;
static uint8_t  g_slp_typa        = 0;
static uint8_t  g_slp_typb        = 0;

static int        g_reset_supported = 0;
static acpi_gas_t  g_reset_reg;
static uint8_t     g_reset_value    = 0;

 

static int memcmp_local(const void *a, const void *b, size_t n) {
    const uint8_t *p = (const uint8_t *)a, *q = (const uint8_t *)b;
    for (size_t i = 0; i < n; i++) if (p[i] != q[i]) return (int)p[i] - (int)q[i];
    return 0;
}

 


static int checksum_ok(const void *table, uint32_t len) {
    const uint8_t *p = (const uint8_t *)table;
    uint8_t sum = 0;
    for (uint32_t i = 0; i < len; i++) sum = (uint8_t)(sum + p[i]);
    return sum == 0;
}

 










static int parse_s5(const uint8_t *tbl, uint32_t len, uint8_t *out_a, uint8_t *out_b) {
    if (len < 9) return -1;
    for (uint32_t i = 0; i + 4 < len; i++) {
        if (!(tbl[i] == '_' && tbl[i+1] == 'S' && tbl[i+2] == '5' && tbl[i+3] == '_'))
            continue;

        uint32_t p = i + 4;
        if (p >= len || tbl[p] != 0x12) continue;    
        p++;
        if (p >= len) continue;

        uint32_t extra = (uint32_t)((tbl[p] >> 6) & 0x3);   
        p += extra + 1;                                     
        if (p >= len) continue;
        p++;                                                 
        if (p >= len) continue;

        if (tbl[p] == 0x0A) { p++; if (p >= len) continue; }  
        uint8_t a = tbl[p++];
        if (p >= len) continue;
        if (tbl[p] == 0x0A) { p++; if (p >= len) continue; }
        uint8_t b = tbl[p];

        *out_a = a;
        *out_b = b;
        return 0;
    }
    return -1;
}

 


int acpi_init(uint64_t rsdp_phys) {
    g_fadt_ok = 0;
    g_s5_found = 0;
    g_reset_supported = 0;

    if (rsdp_phys == 0) {
        DBG_MSG("ACPI", "no RSDP from bootloader");
        return -1;
    }

    const acpi_rsdp_t *rsdp = (const acpi_rsdp_t *)phys_to_kvirt(rsdp_phys);
    if (memcmp_local(rsdp->signature, "RSD PTR ", 8) != 0) {
        DBG_MSG("ACPI", "bad RSDP signature");
        return -1;
    }
    if (!checksum_ok(rsdp, 20)) {
        DBG_MSG("ACPI", "bad RSDP checksum (v1 range)");
        return -1;
    }

    int      use_xsdt = (rsdp->revision >= 2 && rsdp->xsdt_address != 0);
    uint64_t root_phys = use_xsdt ? rsdp->xsdt_address : (uint64_t)rsdp->rsdt_address;
    if (root_phys == 0) {
        DBG_MSG("ACPI", "no RSDT/XSDT address");
        return -1;
    }

    const acpi_sdt_header_t *root = (const acpi_sdt_header_t *)phys_to_kvirt(root_phys);
    const char *want_sig = use_xsdt ? "XSDT" : "RSDT";
    if (memcmp_local(root->signature, want_sig, 4) != 0) {
        DBG_MSG("ACPI", "bad RSDT/XSDT signature");
        return -1;
    }
    if (!checksum_ok(root, root->length)) {
        DBG_MSG("ACPI", "bad RSDT/XSDT checksum");
        return -1;
    }

    const uint8_t *entries = (const uint8_t *)root + sizeof(acpi_sdt_header_t);
    uint32_t entry_size  = use_xsdt ? 8 : 4;
    uint32_t entry_count = (root->length - (uint32_t)sizeof(acpi_sdt_header_t)) / entry_size;

    const acpi_fadt_t *fadt = NULL;
     
    #define MAX_SSDT 16
    const acpi_sdt_header_t *ssdt_list[MAX_SSDT];
    uint32_t ssdt_count = 0;

    for (uint32_t i = 0; i < entry_count; i++) {
        uint64_t tbl_phys;
        if (use_xsdt) {
            uint64_t v; memcpy(&v, entries + (uint64_t)i * 8, 8); tbl_phys = v;
        } else {
            uint32_t v; memcpy(&v, entries + (uint64_t)i * 4, 4); tbl_phys = v;
        }
        if (tbl_phys == 0) continue;

        const acpi_sdt_header_t *hdr = (const acpi_sdt_header_t *)phys_to_kvirt(tbl_phys);
        if (memcmp_local(hdr->signature, "FACP", 4) == 0) {
            if (checksum_ok(hdr, hdr->length)) fadt = (const acpi_fadt_t *)hdr;
        } else if (memcmp_local(hdr->signature, "SSDT", 4) == 0) {
            if (ssdt_count < MAX_SSDT) ssdt_list[ssdt_count++] = hdr;
        }
    }

    if (!fadt) {
        DBG_MSG("ACPI", "FADT (FACP) not found");
        return -1;
    }

    g_smi_cmd         = fadt->smi_cmd;
    g_acpi_enable_val = fadt->acpi_enable;
    g_pm1a_cnt_blk    = fadt->pm1a_cnt_blk;
    g_pm1b_cnt_blk    = fadt->pm1b_cnt_blk;

     


    if (fadt->header.length >= FADT_MIN_LEN_X_DSDT) {
        if (fadt->x_pm1a_cnt_blk.address != 0 && fadt->x_pm1a_cnt_blk.address_space_id == 1)
            g_pm1a_cnt_blk = (uint32_t)fadt->x_pm1a_cnt_blk.address;
        if (fadt->x_pm1b_cnt_blk.address != 0 && fadt->x_pm1b_cnt_blk.address_space_id == 1)
            g_pm1b_cnt_blk = (uint32_t)fadt->x_pm1b_cnt_blk.address;
    }

    DBG_VAL("ACPI", "PM1a_CNT_BLK", g_pm1a_cnt_blk);

     
    if (fadt->header.length >= FADT_MIN_LEN_RESET_REG &&
        (fadt->flags & FADT_FLAG_RESET_REG_SUP) &&
        (fadt->reset_reg.address_space_id == 0 || fadt->reset_reg.address_space_id == 1) &&
        fadt->reset_reg.address != 0) {
        g_reset_reg      = fadt->reset_reg;
        g_reset_value    = fadt->reset_value;
        g_reset_supported = 1;
        DBG_MSG("ACPI", "reset register supported");
    } else {
        DBG_MSG("ACPI", "reset register NOT supported");
    }

     
    uint64_t dsdt_phys = fadt->dsdt;
    if (fadt->header.length >= FADT_MIN_LEN_X_DSDT && fadt->x_dsdt != 0)
        dsdt_phys = fadt->x_dsdt;

    if (dsdt_phys != 0) {
        const acpi_sdt_header_t *dsdt = (const acpi_sdt_header_t *)phys_to_kvirt(dsdt_phys);
        if (memcmp_local(dsdt->signature, "DSDT", 4) == 0 && checksum_ok(dsdt, dsdt->length)) {
            const uint8_t *body = (const uint8_t *)dsdt + sizeof(acpi_sdt_header_t);
            uint32_t body_len = dsdt->length - (uint32_t)sizeof(acpi_sdt_header_t);
            if (parse_s5(body, body_len, &g_slp_typa, &g_slp_typb) == 0) g_s5_found = 1;
        }
    }

     

    for (uint32_t i = 0; !g_s5_found && i < ssdt_count; i++) {
        const acpi_sdt_header_t *ssdt = ssdt_list[i];
        if (!checksum_ok(ssdt, ssdt->length)) continue;
        const uint8_t *body = (const uint8_t *)ssdt + sizeof(acpi_sdt_header_t);
        uint32_t body_len = ssdt->length - (uint32_t)sizeof(acpi_sdt_header_t);
        if (parse_s5(body, body_len, &g_slp_typa, &g_slp_typb) == 0) g_s5_found = 1;
    }

    if (g_s5_found) {
        DBG_VAL("ACPI", "SLP_TYPa", g_slp_typa);
        DBG_VAL("ACPI", "SLP_TYPb", g_slp_typb);
    } else {
        DBG_MSG("ACPI", "\\_S5 not found in DSDT/SSDT");
    }

    g_fadt_ok = 1;
    return 0;
}

 
static void acpi_enable(void) {
    if (g_pm1a_cnt_blk == 0) return;
    if (acpi_inw((uint16_t)g_pm1a_cnt_blk) & 0x1) return;  
    if (g_smi_cmd == 0 || g_acpi_enable_val == 0) return;   

    acpi_outb((uint16_t)g_smi_cmd, g_acpi_enable_val);
    for (int i = 0; i < 300000; i++) {
        if (acpi_inw((uint16_t)g_pm1a_cnt_blk) & 0x1) return;
        acpi_io_wait();
    }
    DBG_MSG("ACPI", "acpi_enable: SCI_EN timeout (продолжаем всё равно)");
}

int acpi_power_off(void) {
    if (!g_fadt_ok || !g_s5_found || g_pm1a_cnt_blk == 0) {
        DBG_MSG("ACPI", "power_off: not supported (no FADT/_S5/PM1a_CNT)");
        return -1;
    }

    DBG_MSG("ACPI", "power_off: enabling ACPI mode...");
    acpi_enable();

    DBG_MSG("ACPI", "power_off: writing PM1_CNT (SLP_TYP|SLP_EN)...");
    uint16_t val_a = (uint16_t)(((uint16_t)g_slp_typa << 10) | (1u << 13));
    acpi_outw((uint16_t)g_pm1a_cnt_blk, val_a);
    if (g_pm1b_cnt_blk != 0) {
        uint16_t val_b = (uint16_t)(((uint16_t)g_slp_typb << 10) | (1u << 13));
        acpi_outw((uint16_t)g_pm1b_cnt_blk, val_b);
    }

     

    for (volatile uint32_t i = 0; i < 30000000u; i++) { }
    DBG_MSG("ACPI", "power_off: did not power off (unexpected)");
    return -1;
}

int acpi_reset(void) {
    if (!g_fadt_ok || !g_reset_supported) {
        DBG_MSG("ACPI", "reset: not supported (no RESET_REG_SUP)");
        return -1;
    }

    DBG_MSG("ACPI", "reset: writing ACPI Reset Register...");
    if (g_reset_reg.address_space_id == 1) {
        acpi_outb((uint16_t)g_reset_reg.address, g_reset_value);
    } else {
        volatile uint8_t *reg = (volatile uint8_t *)phys_to_kvirt(g_reset_reg.address);
        *reg = g_reset_value;
    }

    for (volatile uint32_t i = 0; i < 30000000u; i++) { }
    DBG_MSG("ACPI", "reset: did not reboot (unexpected)");
    return -1;
}

void cpu_triple_fault_reset(void) {
    DBG_MSG("ACPI", "triple_fault_reset: last resort");
    struct __attribute__((packed)) { uint16_t limit; uint64_t base; } idtr = { 0, 0 };
    __asm__ volatile ("cli" ::: "memory");
    __asm__ volatile ("lidt %0" : : "m"(idtr));
    __asm__ volatile ("int $0x03");
    for (;;) __asm__ volatile ("hlt");
}
