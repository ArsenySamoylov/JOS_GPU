#include <inc/types.h>
#include <inc/assert.h>
#include <inc/string.h>
#include <inc/memlayout.h>
#include <inc/stdio.h>
#include <inc/x86.h>
#include <inc/uefi.h>
#include <kern/timer.h>
#include <kern/kclock.h>
#include <kern/picirq.h>
#include <kern/trap.h>
#include <kern/pmap.h>

#define kilo      (1000ULL)
#define Mega      (kilo * kilo)
#define Giga      (kilo * Mega)
#define Tera      (kilo * Giga)
#define Peta      (kilo * Tera)
#define ULONG_MAX ~0UL

#if LAB <= 6
/* Early variant of memory mapping that does 1:1 aligned area mapping
 * in 2MB pages. You will need to reimplement this code with proper
 * virtual memory mapping in the future. */
void *
mmio_map_region(physaddr_t pa, size_t size) {
    void map_addr_early_boot(uintptr_t addr, uintptr_t addr_phys, size_t sz);
    const physaddr_t base_2mb = 0x200000;
    uintptr_t org = pa;
    size += pa & (base_2mb - 1);
    size += (base_2mb - 1);
    pa &= ~(base_2mb - 1);
    size &= ~(base_2mb - 1);
    map_addr_early_boot(pa, pa, size);
    return (void *)org;
}
void *
mmio_remap_last_region(physaddr_t pa, void *addr, size_t oldsz, size_t newsz) {
    return mmio_map_region(pa, newsz);
}
#endif

struct Timer timertab[MAX_TIMERS];
struct Timer *timer_for_schedule;

struct Timer timer_hpet0 = {
        .timer_name = "hpet0",
        .timer_init = hpet_init,
        .get_cpu_freq = hpet_cpu_frequency,
        .enable_interrupts = hpet_enable_interrupts_tim0,
        .handle_interrupts = hpet_handle_interrupts_tim0,
};

struct Timer timer_hpet1 = {
        .timer_name = "hpet1",
        .timer_init = hpet_init,
        .get_cpu_freq = hpet_cpu_frequency,
        .enable_interrupts = hpet_enable_interrupts_tim1,
        .handle_interrupts = hpet_handle_interrupts_tim1,
};

struct Timer timer_acpipm = {
        .timer_name = "pm",
        .timer_init = acpi_enable,
        .get_cpu_freq = pmtimer_cpu_frequency,
};

void
acpi_enable(void) {
    FADT *fadt = get_fadt();
    outb(fadt->SMI_CommandPort, fadt->AcpiEnable);
    while ((inw(fadt->PM1aControlBlock) & 1) == 0) /* nothing */
        ;
}

static void *
acpi_find_table(const char *sign) {
    /*
     * This function performs lookup of ACPI table by its signature
     * and returns valid pointer to the table mapped somewhere.
     *
     * It is a good idea to checksum tables before using them.
     *
     * HINT: Use mmio_map_region/mmio_remap_last_region
     * before accessing table addresses
     * (Why mmio_remap_last_region is requrired?)
     * HINT: RSDP address is stored in uefi_lp->ACPIRoot
     * HINT: You may want to distunguish RSDT/XSDT
     */
    // LAB 5: Your code here:
    RSDP* rsd_ptr = (RSDP*) uefi_lp->ACPIRoot;
    // todo: check_sum rsd_ptr
    // todo: mapping?

    RSDT* rsdt = NULL;
    switch(rsd_ptr->Revision) {
        case 0: rsdt = (RSDT*) (uintptr_t) rsd_ptr->RsdtAddress; break;
        case 2: rsdt = (RSDT*) rsd_ptr->XsdtAddress; break;
        default: panic("Wrong ACPI version");
    }

    // todo: validate rsdt
    int n_sdt = (rsdt->h.Length - sizeof(rsdt->h)) / sizeof(rsdt->PointerToOtherSDT[0]);
    for (int i = 0; i < n_sdt; ++i) {
        ACPISDTHeader* header = *((ACPISDTHeader**) rsdt->PointerToOtherSDT + i);
        // todo: validate
        // cprintf("%s\n", header->Signature);
        if (!strncmp(header->Signature, sign, 4))
            return header;
    }

    return NULL;
}

/* Obtain and map FADT ACPI table address. */
FADT *
get_fadt(void) {
    // LAB 5: Your code here
    // (use acpi_find_table)
    // HINT: ACPI table signatures are
    //       not always as their names
    ACPISDTHeader* header = acpi_find_table("FACP");
    if (!header) panic("No FADT header");

    FADT* fad = (FADT*) header;
    return fad;
}

/* Obtain and map RSDP ACPI table address. */
HPET *
get_hpet(void) {
    // LAB 5: Your code here
    // (use acpi_find_table)
    ACPISDTHeader* header = acpi_find_table("HPET");
    if (!header) panic("No HPET header");
    // map hpet address ?
    return (HPET*) header;
}

/* Getting physical HPET timer address from its table. */
HPETRegister *
hpet_register(void) {
    HPET *hpet_timer = get_hpet();
    if (!hpet_timer->address.address) panic("hpet is unavailable\n");

    uintptr_t paddr = hpet_timer->address.address;
    return mmio_map_region(paddr, sizeof(HPETRegister));
}

/* Debug HPET timer state. */
void
hpet_print_struct(void) {
    HPET *hpet = get_hpet();
    assert(hpet != NULL);
    cprintf("signature = %s\n", (hpet->h).Signature);
    cprintf("length = %08x\n", (hpet->h).Length);
    cprintf("revision = %08x\n", (hpet->h).Revision);
    cprintf("checksum = %08x\n", (hpet->h).Checksum);

    cprintf("oem_revision = %08x\n", (hpet->h).OEMRevision);
    cprintf("creator_id = %08x\n", (hpet->h).CreatorID);
    cprintf("creator_revision = %08x\n", (hpet->h).CreatorRevision);

    cprintf("hardware_rev_id = %08x\n", hpet->hardware_rev_id);
    cprintf("comparator_count = %08x\n", hpet->comparator_count);
    cprintf("counter_size = %08x\n", hpet->counter_size);
    cprintf("reserved = %08x\n", hpet->reserved);
    cprintf("legacy_replacement = %08x\n", hpet->legacy_replacement);
    cprintf("pci_vendor_id = %08x\n", hpet->pci_vendor_id);
    cprintf("hpet_number = %08x\n", hpet->hpet_number);
    cprintf("minimum_tick = %08x\n", hpet->minimum_tick);

    cprintf("address_structure:\n");
    cprintf("address_space_id = %08x\n", (hpet->address).address_space_id);
    cprintf("register_bit_width = %08x\n", (hpet->address).register_bit_width);
    cprintf("register_bit_offset = %08x\n", (hpet->address).register_bit_offset);
    cprintf("address = %08lx\n", (unsigned long)(hpet->address).address);
}

static volatile HPETRegister *hpetReg;
/* HPET timer period (in femtoseconds) */
static uint64_t hpetFemto = 0;
/* HPET timer frequency */
static uint64_t hpetFreq = 0;

/* HPET timer initialisation */
void
hpet_init() {
    if (hpetReg == NULL) {
        nmi_disable();
        hpetReg = hpet_register();
        uint64_t cap = hpetReg->GCAP_ID;
        hpetFemto = (uintptr_t)(cap >> 32);
        if (!(cap & HPET_LEG_RT_CAP)) panic("HPET has no LegacyReplacement mode");

        // cprintf("hpetFemto = %llu\n", hpetFemto);
        hpetFreq = (1 * Peta) / hpetFemto;
        cprintf("HPET: Frequency = %lu.%03luMHz\n", (uintptr_t)(hpetFreq / Mega), (uintptr_t)(hpetFreq % Mega));
        /* Enable ENABLE_CNF bit to enable timer */
        hpetReg->GEN_CONF |= HPET_ENABLE_CNF;
        nmi_enable();
    }
}

/* HPET register contents debugging. */
void
hpet_print_reg(void) {
    cprintf("GCAP_ID = %016lx\n", (unsigned long)hpetReg->GCAP_ID);
    cprintf("GEN_CONF = %016lx\n", (unsigned long)hpetReg->GEN_CONF);
    cprintf("GINTR_STA = %016lx\n", (unsigned long)hpetReg->GINTR_STA);
    cprintf("MAIN_CNT = %016lx\n", (unsigned long)hpetReg->MAIN_CNT);
    cprintf("TIM0_CONF = %016lx\n", (unsigned long)hpetReg->TIM0_CONF);
    cprintf("TIM0_COMP = %016lx\n", (unsigned long)hpetReg->TIM0_COMP);
    cprintf("TIM0_FSB = %016lx\n", (unsigned long)hpetReg->TIM0_FSB);
    cprintf("TIM1_CONF = %016lx\n", (unsigned long)hpetReg->TIM1_CONF);
    cprintf("TIM1_COMP = %016lx\n", (unsigned long)hpetReg->TIM1_COMP);
    cprintf("TIM1_FSB = %016lx\n", (unsigned long)hpetReg->TIM1_FSB);
    cprintf("TIM2_CONF = %016lx\n", (unsigned long)hpetReg->TIM2_CONF);
    cprintf("TIM2_COMP = %016lx\n", (unsigned long)hpetReg->TIM2_COMP);
    cprintf("TIM2_FSB = %016lx\n", (unsigned long)hpetReg->TIM2_FSB);
}

/* HPET main timer counter value. */
uint64_t
hpet_get_main_cnt(void) {
    return hpetReg->MAIN_CNT;
}

/* - Configure HPET timer 0 to trigger every 0.5 seconds on IRQ_TIMER line
 * - Configure HPET timer 1 to trigger every 1.5 seconds on IRQ_CLOCK line
 *
 * HINT To be able to use HPET as PIT replacement consult
 *      LegacyReplacement functionality in HPET spec.
 * HINT Don't forget to unmask interrupt in PIC */
void
hpet_enable_interrupts_tim0(void) {
    // LAB 5: Your code here
    if(!hpetReg) panic("No hpetReg");

    hpetReg->GEN_CONF |= HPET_LEG_RT_CNF;

    hpetReg->TIM0_CONF |= HPET_TN_VAL_SET_CNF;
    hpetReg->TIM0_CONF |= (HPET_TN_INT_ENB_CNF | HPET_TN_TYPE_CNF);
    hpetReg->TIM0_COMP = hpetFreq * 0.5;

    pic_irq_unmask(IRQ_TIMER); 
}

void
hpet_enable_interrupts_tim1(void) {
    // LAB 5: Your code here
    if(!hpetReg) panic("No hpetReg");

    hpetReg->GEN_CONF |= HPET_LEG_RT_CNF;

    hpetReg->TIM1_CONF |= HPET_TN_VAL_SET_CNF;
    hpetReg->TIM1_CONF |= (HPET_TN_INT_ENB_CNF | HPET_TN_TYPE_CNF);
    hpetReg->TIM1_COMP = hpetFreq * 1.5;

    pic_irq_unmask(IRQ_CLOCK);
}

void
hpet_handle_interrupts_tim0(void) {
    pic_send_eoi(IRQ_TIMER);
}

void
hpet_handle_interrupts_tim1(void) {
    pic_send_eoi(IRQ_CLOCK);
}

static void 
hpet_measure_ticks(const uint64_t HpetTicksDuration, uint64_t* HpetTicksDelta, uint64_t* TicksDelta) {
    uint64_t hpet_tick0, hpet_tick1;
    uint64_t hpet_current_delta;

    uint64_t tsc_tick0, tsc_tick1;

    hpet_tick0 = hpet_get_main_cnt();
    tsc_tick0  = read_tsc();

    do {
    asm volatile ("pause");

    hpet_tick1 = hpet_get_main_cnt();

    if (hpet_tick0 < hpet_tick1)
        hpet_current_delta = hpet_tick1 - hpet_tick0;
    else
        hpet_current_delta = UINT64_MAX - hpet_tick0 + hpet_tick1;
    
    } while(hpet_current_delta < HpetTicksDuration);

    tsc_tick1 = read_tsc();
    
    if (tsc_tick0 < tsc_tick1)
        *TicksDelta =  tsc_tick1 - tsc_tick0;
    else
        *TicksDelta =  UINT64_MAX + tsc_tick1 - tsc_tick0;
    
    *HpetTicksDelta = hpet_current_delta;
}

/* Calculate CPU frequency in Hz with the help with HPET timer.
 * HINT Use hpet_get_main_cnt function and do not forget about
 * about pause instruction. */
uint64_t
hpet_cpu_frequency(void) {
    static uint64_t cpu_freq;

    // LAB 5: Your code here
    if (!cpu_freq) {
        if (!hpetFreq) panic("No HpaetFreq\n");
        const uint64_t HpetTicksDuration = hpetFreq / 10;
         
        uint64_t HpetTicksDelta;
        uint64_t TscTicksDelta;

        hpet_measure_ticks(HpetTicksDuration, &HpetTicksDelta, &TscTicksDelta);

        cpu_freq = (TscTicksDelta * hpetFreq) / HpetTicksDelta;
    }

    return cpu_freq;
}

uint32_t
pmtimer_get_timeval(void) {
    FADT *fadt = get_fadt();
    return inl(fadt->PMTimerBlock);
}

#define UINT24_MAX 0x00ffffffU

static void 
AsmMeasureTicks (uint32_t AcpiTicksDuration, uint32_t *AcpiTicksDelta, uint64_t *TscTicksDelta) {
  uint32_t  AcpiTick0;
  uint32_t  AcpiTick1;
  uint32_t  AcpiCurrentDelta;
  uint64_t  Tsc0;
  uint64_t  Tsc1;

  AcpiTick0 = pmtimer_get_timeval();
  Tsc0      = read_tsc();

  do {
    asm volatile ("pause");

    //
    // Check how many AcpiTicks have passed since we started.
    //
    AcpiTick1 = pmtimer_get_timeval();

    if (AcpiTick0 <= AcpiTick1) {
      //
      // No overflow.
      //
      AcpiCurrentDelta = AcpiTick1 - AcpiTick0;
    } else if (AcpiTick0 - AcpiTick1 <= UINT24_MAX) {
      //
      // Overflow, 24-bit timer.
      //
      AcpiCurrentDelta = UINT24_MAX - AcpiTick0 + AcpiTick1;
    } else {
      //
      // Overflow, 32-bit timer.
      //
      AcpiCurrentDelta = UINT32_MAX - AcpiTick0 + AcpiTick1;
    }

    //
    // Keep checking AcpiTicks until target is reached.
    //
  } while (AcpiCurrentDelta < AcpiTicksDuration);

  Tsc1 = read_tsc();

  //
  // On some systems we may end up waiting for notably longer than 100ms,
  // despite disabling all events. Divide by actual time passed as suggested
  // by asava's Clover patch r2668.
  //
  *TscTicksDelta  = Tsc1 - Tsc0;
  *AcpiTicksDelta = AcpiCurrentDelta;
}

/* Calculate CPU frequency in Hz with the help with ACPI PowerManagement timer.
 * HINT Use pmtimer_get_timeval function and do not forget that ACPI PM timer
 *      can be 24-bit or 32-bit. */
uint64_t
pmtimer_cpu_frequency(void) {
    static uint64_t cpu_freq;

    // LAB 5: Your code here
    if (!cpu_freq) {        
        uint64_t AcpiTicksDuration = PM_FREQ / 10;
        uint32_t AcpiTicksDelta;
        uint64_t TscTicksDelta;

        AsmMeasureTicks (AcpiTicksDuration, &AcpiTicksDelta, &TscTicksDelta);

        cpu_freq = (TscTicksDelta * PM_FREQ) / AcpiTicksDelta;
    }
    
    return cpu_freq;
}
