// Machdyne BASIC for the Zwolf LS10A: a Sechs module.
// UART receive is based on the ch32fun usart_dma_rx_circular example.

#include "ch32fun/ch32fun/ch32fun.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "ls10.h"
#include "../../basic.h"
#include "../../sechs/sechs.h"

#define RX_BUF_LEN 32       // UART receive ring (DMA)
#define BOOT_WINDOW_MS 600  // Sechs: at least 500, at most 1000

// The filesystem uses the F-RAM up to FS_SIZE; the Sechs address is kept
// in the last bytes: 0xA5, address, complement.
#define FS_SIZE (FRAM_SIZE - 16)
#define CFG_ADDR (FRAM_SIZE - 16)

uint8_t rx_buf[RX_BUF_LEN];
static u32 tail;            // read position in rx_buf
static u32 cmd_len;         // characters collected in basic_line (the
                            // console line: shared with LOAD, for RAM)

static uint8_t uart_awake;  // the UART console has been woken
static uint8_t halted;

void fram_init(void);

static void put_str(const char *s) {
    while (*s) hw_putc(*s++);
}

static u32 rx_head(void) {
    return (RX_BUF_LEN - DMA1_Channel5->CNTR) % RX_BUF_LEN;
}

static void pin_cfg_raw(GPIO_TypeDef *port, uint8_t bit, uint32_t cfg) {
    port->CFGLR &= ~(0xf << (4 * bit));
    port->CFGLR |= cfg << (4 * bit);
}

// ---- Sechs I2C target on A (PC2, SCL) and B (PC1, SDA) -------------------

static void i2c_target_on(void) {
    pin_cfg_raw(GPIOC, ZW_GPIOA, GPIO_Speed_10MHz | GPIO_CNF_OUT_OD_AF);
    pin_cfg_raw(GPIOC, ZW_GPIOB, GPIO_Speed_10MHz | GPIO_CNF_OUT_OD_AF);
    I2C1->CTLR1 |= I2C_CTLR1_PE | I2C_CTLR1_ACK;
}

static void i2c_target_init(void) {
    uint8_t c[3];
    fs_media_read(CFG_ADDR, c, 3);
    // the stored address and its complement must agree; sechs_init also
    // refuses one outside 0x08-0x77 (blank F-RAM fails both)
    sechs_init(c[2] == (uint8_t)~c[1] ? c[1] : 0,
        CAP_FILES | CAP_UART_CON | CAP_I2C_CON);
    RCC->APB2PCENR |= RCC_APB2Periph_GPIOC | RCC_APB2Periph_AFIO;
    RCC->APB1PCENR |= RCC_APB1Periph_I2C1;
    RCC->APB1PRSTR |= RCC_APB1Periph_I2C1;
    RCC->APB1PRSTR &= ~RCC_APB1Periph_I2C1;
    I2C1->CTLR2 = (FUNCONF_SYSTEM_CORE_CLOCK / 2000000) | I2C_CTLR2_ITBUFEN |
        I2C_CTLR2_ITEVTEN | I2C_CTLR2_ITERREN;
    I2C1->CKCFGR = (FUNCONF_SYSTEM_CORE_CLOCK / 300000) | I2C_CKCFGR_FS;
    I2C1->OADDR1 = sechs.addr << 1;
    NVIC_EnableIRQ(I2C1_EV_IRQn);
    NVIC_EnableIRQ(I2C1_ER_IRQn);
    i2c_target_on();
}

// One handler for both I2C vectors (events and errors). A read ends with
// the controller's NACK (AF); the byte loaded after the last one sent was
// never sent.
void I2C1_EV_IRQHandler(void) __attribute__((interrupt));
void I2C1_ER_IRQHandler(void) __attribute__((interrupt, alias("I2C1_EV_IRQHandler")));
void I2C1_EV_IRQHandler(void) {
    uint16_t s1 = I2C1->STAR1;
    uint16_t s2 = I2C1->STAR2;
    (void)s2;               // reading STAR2 completes the ADDR event
    if (s1 & I2C_STAR1_ADDR) sechs_start(0);
    if (s1 & I2C_STAR1_RXNE) sechs_rx(I2C1->DATAR);
    if (s1 & I2C_STAR1_TXE) I2C1->DATAR = sechs_tx();
    if (s1 & I2C_STAR1_STOPF) {
        I2C1->CTLR1 |= 0;           // clears STOPF (after reading STAR1)
        sechs_stop(0);
    }
    if (s1 & (I2C_STAR1_AF | I2C_STAR1_BERR | I2C_STAR1_ARLO | I2C_STAR1_OVR)) {
        if (s1 & I2C_STAR1_AF) sechs_stop(1);
        I2C1->STAR1 = 0;
    }
}

// Called in the I2C interrupt: the new address applies at once, and is
// saved to F-RAM by service() in the main loop, never during another F-RAM
// access.
static volatile uint8_t addr_save;

void sechs_set_addr(uint8_t addr) {
    I2C1->OADDR1 = addr << 1;
    addr_save = 1;
}

uint8_t *sechs_regs(void) {
    return basic_regs;
}

// INFO
static const char info_text[] =
    "fw=Machdyne BASIC\nmod=LS10A\nlang=basic\n";

uint8_t sechs_info(uint8_t i) {
    return i < sizeof(info_text) - 1 ? info_text[i] : 0;
}

// ---- consoles ---------------------------------------------------------------

// The UART console wakes on a CR or LF on C; until then D is not driven.
static void uart_wake(void) {
    uart_awake = 1;
    pin_cfg_raw(GPIOD, ZW_GPIOD, GPIO_Speed_10MHz | GPIO_CNF_OUT_PP_AF);
    put_str("///\r\n");
}

#ifdef DIAG_BREAK
// (a diagnostic build: how much of the stack was never used, after each
// console command; the free RAM is painted at start-up)
extern uint8_t _ebss[];

static void stack_paint(void) {
    uint8_t *sp;
    __asm__ volatile ("mv %0, sp" : "=r"(sp));
    for (uint8_t *p = _ebss; p < sp - 16; p++) *p = 0xA5;
}

static void stack_report(void) {
    uint16_t n = 0;
    char d[6];
    int8_t k = 0;
    while (_ebss[n] == 0xA5) n++;
    do d[k++] = '0' + n % 10; while (n /= 10);
    put_str("[free ");
    while (k) hw_putc(d[--k]);
    put_str("]\r\n");
}
#endif

static void console_char(uint8_t c) {
    hw_putc(c);     // echo
    if (c == '\r' || c == '\n') {
        if (c == '\r') hw_putc('\n');
        basic_line[cmd_len] = '\0';
        cmd_len = 0;
        basic_yield((uint8_t *)basic_line);
#ifdef DIAG_BREAK
        stack_report();
#endif
    } else if (cmd_len < BASIC_LINE - 1) {
        basic_line[cmd_len++] = c;
    }
}

// Keep the Sechs registers up to date and act on CONTROL commands.
// Called from the main loop and, while a program runs, from hw_break().
static void service(void) {
    uint8_t cmd = sechs.cmd;
    uint8_t deg = fs_degraded();
    uint8_t fault = basic_prog_err == BASIC_E_BUS ? FAULT_BUS :
        basic_prog_err && basic_prog_err != BASIC_E_BREAK ? FAULT_PROGRAM : 0;
    sechs.cmd = 0;
    sechs.r[SR_FAULT] = fault;
    sechs.r[SR_STATUS] = (sechs.r[SR_STATUS] & ST_BOOT) |
        (sechs.networked ? ST_NETWORKED : 0) | (halted ? ST_HALTED : 0) |
        (basic_running ? ST_RUNNING : 0) | (fault ? ST_FAULT : 0) |
        ((uart_awake || sechs.con_active) ? ST_CONSOLE : 0) |
        (deg ? ST_DEGRADED : 0);
    // OK: every bit 1 means good (fault, degraded and halted from STATUS)
    uint8_t s = sechs.r[SR_STATUS];
    sechs.r[SR_OK] = ~(((s >> 4) & 0x06) | ((s << 2) & 0x08) |
        (basic_cmd_err ? 0x10 : 0)) & 0x1F;
    if (addr_save) {
        uint8_t a = sechs.addr, c[3] = { 0xA5, a, (uint8_t)~a };
        addr_save = 0;
        fs_media_prog(CFG_ADDR, c, 3);
    }
    if (cmd == CMD_RESET) NVIC_SystemReset();
    if (cmd == CMD_HALT) halted = 1;
    if (cmd == CMD_RUN) {
        halted = 0;
        if (!basic_running) basic_yield((uint8_t *)"RUN");
    }
}

// New UART bytes: console input once awake; before that, a CR or LF wakes
// the console. Returns 1 if it woke.
static uint8_t uart_poll(void) {
    uint8_t woke = 0;
    while (tail != rx_head()) {
        uint8_t c = rx_buf[tail];
        tail = (tail + 1) % RX_BUF_LEN;
        if (uart_awake) console_char(c);
        else if (c == '\r' || c == '\n') {
            uart_wake();
            woke = 1;
        }
    }
    return woke;
}

int main()
{
	SystemInit();
#ifdef DIAG_BREAK
	stack_paint();
#endif
	// ch32fun connected the UART transmitter to D: disconnect it, so that
	// D is not driven until the console is woken (the UART itself stays
	// on for the hard fault printer).
	pin_cfg_raw(GPIOD, ZW_GPIOD, GPIO_CNF_IN_FLOATING);
	funGpioInitAll();
	fram_init();

	fs_mount(FS_SIZE);     // file commands report NOT FORMATTED if needed
	i2c_target_init();

	// UART receive on C through DMA
	USART1->CTLR1 |= USART_CTLR1_RE;
	USART1->CTLR3 |= USART_CTLR3_DMAR;
	RCC->AHBPCENR |= RCC_DMA1EN;
	DMA1_Channel5->MADDR = (u32)&rx_buf;
	DMA1_Channel5->PADDR = (u32)&USART1->DATAR;
	DMA1_Channel5->CNTR = RX_BUF_LEN;
	DMA1_Channel5->CFGR = DMA_CFGR1_CIRC | DMA_CFGR1_MINC | DMA_CFGR1_EN;

	// BOOT.BAS is loaded now (so that INFO shows its PINS) and run after
	// the boot window, unless the console is woken or a controller halts
	// the module.
	uint8_t loaded = basic_boot();
	sechs.r[SR_STATUS] = ST_BOOT;
	for (uint16_t t = 0; t < BOOT_WINDOW_MS && !halted; t++) {
		Delay_Ms(1);
		service();
		if (uart_poll()) halted = 1;
	}
	sechs.r[SR_STATUS] &= ~ST_BOOT;
	if (loaded && !halted) basic_yield((uint8_t *)"RUN");

	while (1)
	{
		service();
		int c;
		while ((c = sechs_getc()) >= 0) console_char(c);
		uart_poll();
	}
}

// ---- console and time --------------------------------------------------

// the I2C console's output is full: the controller reads meanwhile, in the
// I2C interrupt (255 of these, about half a second, before giving up)
void sechs_wait(void) {
    Delay_Ms(2);
}

void hw_putc(char c) {
    if (uart_awake) putchar(c);
    if (sechs.con_active) sechs_putc(c);
}

// A running program stops on Ctrl-C (UART or I2C console) or a Sechs
// HALT. On the UART, Ctrl-C must be the last character received; it and
// anything typed before it are discarded.
// (DIAG_BREAK, a diagnostic build: say which condition stopped a program)
#ifdef DIAG_BREAK
#define WHY(s) put_str(s)
#else
#define WHY(s)
#endif

int hw_break(void) {
    if (sechs.cmd == CMD_HALT || sechs.con_break) {
        WHY(sechs.con_break ? "[I2C ^C]" : "[HALT]");
        sechs.con_break = 0;
        service();
        return 1;
    }
    if (sechs.cmd || addr_save) service();
    u32 head = rx_head();
    if (uart_awake && head != tail &&
        rx_buf[(head - 1) % RX_BUF_LEN] == 0x03) {
        WHY("[UART ^C]");
        tail = head;
        return 1;
    }
    return 0;
}

void hw_delay_ms(uint16_t ms) {
    Delay_Ms(ms);
}



// ---- files: the filesystem on the F-RAM ---------------------------------

int hw_fformat(void) {
    return fs_format_default(FS_SIZE);
}

// ---- pins ---------------------------------------------------------------
//
// Pins 1-4 are A (PC2), B (PC1), C (PD6) and D (PD5). C and D are also
// the UART console (C receives, D transmits). The console keeps them until
// a program declares another use; it gets them back when they are declared
// unused again (every RUN starts with PINS NET,NET,-,-). The I2C console
// is not affected.

static GPIO_TypeDef *const pin_port[4] = {
    ZW_GPIOA_PORT, ZW_GPIOB_PORT, ZW_GPIOC_PORT, ZW_GPIOD_PORT
};
static const uint8_t pin_bit[4] = { ZW_GPIOA, ZW_GPIOB, ZW_GPIOC, ZW_GPIOD };
static uint8_t adc_ready;

static void pin_cfg(uint8_t i, uint32_t cfg) {
    pin_cfg_raw(pin_port[i], pin_bit[i], cfg);
}

static void adc_init(void) {
    if (adc_ready) return;
    RCC->CFGR0 &= ~(0x1F << 11);            // ADC clock: HCLK / 2
    RCC->APB2PCENR |= RCC_APB2Periph_ADC1;
    RCC->APB2PRSTR |= RCC_APB2Periph_ADC1;
    RCC->APB2PRSTR &= ~RCC_APB2Periph_ADC1;
    ADC1->RSQR1 = 0;
    ADC1->RSQR2 = 0;
    ADC1->SAMPTR2 |= (7 << (3 * 5)) | (7 << (3 * 6));  // longest sampling
    ADC1->CTLR2 |= ADC_ADON | ADC_EXTSEL;
    ADC1->CTLR2 |= ADC_RSTCAL;
    while (ADC1->CTLR2 & ADC_RSTCAL);
    ADC1->CTLR2 |= ADC_CAL;
    while (ADC1->CTLR2 & ADC_CAL);
    adc_ready = 1;
}

// Sechs: A and B are the I2C target unless a program drives them, which
// is refused once a controller has addressed the module. (The pull-up
// probe of the specification is not implemented, to fit the CH32V003.)
int hw_pin_mode(uint8_t pin, uint8_t mode) {
    uint8_t i = pin - 1;

    if (mode == PM_UART) return HW_ERR_UNSUPPORTED;   // not yet

    if (i < 2) {
        if (mode == PM_OD || mode == PM_PP) {
            if (sechs.networked)
                return HW_ERR_BUS;
            I2C1->CTLR1 &= ~I2C_CTLR1_PE;           // target off
        } else {
            i2c_target_on();    // -, IN and NET: the target stays on
            return 0;
        }
    }

    if (i >= 2) {
        if (mode == PM_NONE) {
            // give the pin back to the console (D only once it is awake)
            if (i == 3) {
                pin_cfg(i, uart_awake ? GPIO_Speed_10MHz | GPIO_CNF_OUT_PP_AF
                                      : GPIO_CNF_IN_FLOATING);
                USART1->CTLR1 |= USART_CTLR1_TE;
            } else {
                pin_cfg(i, GPIO_CNF_IN_FLOATING);
                USART1->CTLR1 |= USART_CTLR1_RE;
            }
            return 0;
        }
        // let the last character leave before the pin is taken
        while (uart_awake && !(USART1->STATR & USART_FLAG_TC));
        USART1->CTLR1 &= ~(i == 3 ? USART_CTLR1_TE : USART_CTLR1_RE);
    }

    // pin configuration for each PM_* mode (I2C: open-drain outputs)
    static const uint8_t cfg[8] = {
        GPIO_CNF_IN_FLOATING, GPIO_CNF_IN_FLOATING,
        GPIO_Speed_10MHz | GPIO_CNF_OUT_OD, GPIO_Speed_10MHz | GPIO_CNF_OUT_PP,
        GPIO_CNF_IN_ANALOG, GPIO_Speed_10MHz | GPIO_CNF_OUT_OD,
        GPIO_CNF_IN_FLOATING, GPIO_CNF_IN_FLOATING
    };
    if (mode == PM_AIN) adc_init();
    if (mode == PM_I2C) pin_port[i]->BSHR = 1 << pin_bit[i];   // released
    pin_cfg(i, cfg[mode]);
    return 0;
}

void hw_pin_write(uint8_t pin, uint8_t level) {
    uint8_t i = pin - 1;
    pin_port[i]->BSHR = 1 << (pin_bit[i] + (level ? 0 : 16));
}

uint8_t hw_pin_read(uint8_t pin) {
    uint8_t i = pin - 1;
    return (pin_port[i]->INDR >> pin_bit[i]) & 1;
}

// C is ADC channel 6 (PD6), D is channel 5 (PD5); 10 bits, 0-1023
int16_t hw_adc(uint8_t pin) {
    if (pin < 3) return -1;
    adc_init();
    ADC1->RSQR3 = pin == 3 ? 6 : 5;
    ADC1->CTLR2 |= ADC_SWSTART;
    while (!(ADC1->STATR & ADC_EOC));
    return ADC1->RDATAR & 0x3ff;
}

// The LED (PD4) lights when the pin is low (to be confirmed on hardware).
void hw_led(uint8_t on) {
    (ZW_GPIOH_PORT)->BSHR = 1 << (ZW_GPIOH + (on ? 16 : 0));
    (ZW_GPIOH_PORT)->CFGLR &= ~(0xf << (4 * ZW_GPIOH));
    (ZW_GPIOH_PORT)->CFGLR |= (GPIO_Speed_10MHz | GPIO_CNF_OUT_PP) << (4 * ZW_GPIOH);
}

#ifdef DIAG_BREAK
// (a diagnostic build has no room for the local I2C controller)
int hw_i2c(uint8_t addr, const uint8_t *w, uint8_t wn, uint8_t *r,
           uint8_t rn) {
    (void)addr; (void)w; (void)wn; (void)r; (void)rn;
    return -1;
}
#else
// ---- local I2C controller on C (SCL) and D (SDA), bit-banged ----------

#define SCL 2
#define SDA 3

static void line(uint8_t i, uint8_t high) {
    hw_pin_write(i + 1, high);
    Delay_Us(4);
    // a device may hold SCL low (clock stretching): wait up to ~1 ms
    for (uint16_t t = 0; high && i == SCL && !hw_pin_read(SCL + 1) && t < 250; t++)
        Delay_Us(4);
}

static uint8_t i2c_byte(uint8_t out, uint8_t ack, uint8_t *in) {
    uint8_t v = 0;
    for (uint8_t b = 0; b < 8; b++) {
        line(SDA, out & 0x80);
        out <<= 1;
        line(SCL, 1);
        v = (v << 1) | hw_pin_read(SDA + 1);
        line(SCL, 0);
    }
    line(SDA, !ack);            // ACK bit: sent when reading
    line(SCL, 1);
    uint8_t nack = hw_pin_read(SDA + 1);
    line(SCL, 0);
    line(SDA, 1);
    if (in) *in = v;
    return nack;
}

static void i2c_start(void) {
    line(SDA, 1);
    line(SCL, 1);
    line(SDA, 0);
    line(SCL, 0);
}

int hw_i2c(uint8_t addr, const uint8_t *w, uint8_t wn, uint8_t *r,
           uint8_t rn) {
    int res = 0;
    if (wn || !rn) {
        i2c_start();
        res = i2c_byte(addr << 1, 0, 0);
        while (!res && wn--) res = i2c_byte(*w++, 0, 0);
    }
    if (!res && rn) {
        i2c_start();
        res = i2c_byte((addr << 1) | 1, 0, 0);
        // when reading, send 0xFF (released) and ACK all but the last byte
        while (!res && rn--) i2c_byte(0xFF, rn != 0, r++);
    }
    line(SDA, 0);               // stop
    line(SCL, 1);
    line(SDA, 1);
    return res ? -1 : 0;
}
#endif
