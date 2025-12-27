// based on ch32fun usart_dma_rx_circular example

#include "ch32fun/ch32fun/ch32fun.h"
#include <stdio.h>
#include <stdint.h>
#include "ls10.h"

//#define RX_BUF_LEN 16 // size of receive circular buffer
#define RX_BUF_LEN 128 // size of receive circular buffer

uint8_t rx_buf[RX_BUF_LEN] = {0}; // DMA receive buffer for incoming data
uint8_t cmd_buf[RX_BUF_LEN] = {0}; // buffer for complete command strings

void basic_yield(uint8_t *line);

int main()
{
	SystemInit();
	funGpioInitAll();

	Delay_Ms(150);
	printf("///\r\n");

	// enable rx pin
	USART1->CTLR1 |= USART_CTLR1_RE;

	// enable usart's dma rx requests
	USART1->CTLR3 |= USART_CTLR3_DMAR;

	// enable dma clock
	RCC->AHBPCENR |= RCC_DMA1EN;

	// configure dma for UART reception, it should fire on RXNE
	DMA1_Channel5->MADDR = (u32)&rx_buf;
	DMA1_Channel5->PADDR = (u32)&USART1->DATAR;
	DMA1_Channel5->CNTR = RX_BUF_LEN;

	// MEM2MEM: 0 (memory to peripheral)
	// PL: 0 (low priority since UART is a relatively slow peripheral)
	// MSIZE/PSIZE: 0 (8-bit)
	// MINC: 1 (increase memory address)
	// PINC: 0 (peripheral address remains unchanged)
	// CIRC: 1 (circular)
	// DIR: 0 (read from peripheral)
	// TEIE: 0 (no tx error interrupt)
	// HTIE: 0 (no half tx interrupt)
	// TCIE: 0 (no transmission complete interrupt)
	// EN: 1 (enable DMA)
	DMA1_Channel5->CFGR = DMA_CFGR1_CIRC | DMA_CFGR1_MINC | DMA_CFGR1_EN;


	while(1)
	{
		static u32 tail = 0; // current read position in rx_buf
		static u32 cmd_end = 0; // end index of current command in rx_buf
		static u32 cmd_st = 0; // start index of current command in rx_buf

		// calculate head position based on DMA counter (modulo when DMA1_Channel5->CNTR = 0)
		u32 head = (RX_BUF_LEN - DMA1_Channel5->CNTR) % RX_BUF_LEN; // current write position in rx_buf
		
		// process new bytes in rx_buf. when a newline character is detected, the command is copied to cmd_buf
		while (tail != head)
		{

			putchar(rx_buf[tail]); // echo			
			if (rx_buf[tail] == '\r') putchar('\n');

			if ( rx_buf[tail] == '\n' || rx_buf[tail] == '\r') 
			{
				cmd_end = tail;
				u32 cmd_i = 0; // carret position in cmd_buf
				if (cmd_end > cmd_st)
				{
					for (u32 rx_i = cmd_st; rx_i < cmd_end + 1; rx_i++, cmd_i++) {
						cmd_buf[cmd_i] = rx_buf[rx_i];
					}
				} else if (cmd_st > cmd_end) { // handle wrap around
					for (u32 rx_i = cmd_st; rx_i < RX_BUF_LEN; rx_i++, cmd_i++) {
						cmd_buf[cmd_i] = rx_buf[rx_i];
					}
					for (u32 rx_i = 0; rx_i < cmd_end + 1; rx_i++, cmd_i++) {
						cmd_buf[cmd_i] = rx_buf[rx_i];
					}
				}

				// null terminate
				cmd_buf[cmd_i] = '\0';

				basic_yield(cmd_buf);

				// update start position for next command
				cmd_st = (cmd_end + 1) % RX_BUF_LEN;
			}

			// move to next position 
			tail = (tail+1) % RX_BUF_LEN;
		}
	}
}

int atoi(const char *str) {
    int result = 0;
    int sign = 1;

    // Skip leading whitespace
    while (*str == ' ' || *str == '\t' || *str == '\n' ||
           *str == '\v' || *str == '\f' || *str == '\r') {
        str++;
    }

    // Check for optional sign
    if (*str == '-') {
        sign = -1;
        str++;
    } else if (*str == '+') {
        str++;
    }

    // Convert digits to integer
    while (*str >= '0' && *str <= '9') {
        result = result * 10 + (*str - '0');
        str++;
    }

    return sign * result;
}

void hw_sleep(uint16_t secs) {
	Delay_Ms(secs * 1000);
}

uint8_t hw_peek(uint8_t addr) {
	return 0; 
}

void hw_poke(uint8_t addr, uint8_t data) {

   if (addr == 0x10) {

      printf(" SET GPIO DIR %2x = %2x\r\n", addr, data);

      if ((data & 0x80) == 0x80) {
			(ZW_GPIOH_PORT)->CFGLR &= ~(0xf<<(4*ZW_GPIOH));
         (ZW_GPIOH_PORT)->CFGLR |= (GPIO_Speed_10MHz | GPIO_CNF_OUT_PP)<<(4*ZW_GPIOH);
      } else {
			(ZW_GPIOH_PORT)->CFGLR &= ~(0xf<<(4*ZW_GPIOH));
         (ZW_GPIOH_PORT)->CFGLR |= (GPIO_Speed_10MHz | GPIO_CNF_IN_FLOATING)<<(4*ZW_GPIOH);
		}

      if ((data & 0x02) == 0x02) {
			(ZW_GPIOB_PORT)->CFGLR &= ~(0xf<<(4*ZW_GPIOB));
         (ZW_GPIOB_PORT)->CFGLR |= (GPIO_Speed_10MHz | GPIO_CNF_OUT_PP)<<(4*ZW_GPIOB);
      } else {
			(ZW_GPIOB_PORT)->CFGLR &= ~(0xf<<(4*ZW_GPIOB));
         (ZW_GPIOB_PORT)->CFGLR |= (GPIO_Speed_10MHz | GPIO_CNF_IN_FLOATING)<<(4*ZW_GPIOB);

		}

      if ((data & 0x01) == 0x01) {
			(ZW_GPIOA_PORT)->CFGLR &= ~(0xf<<(4*ZW_GPIOA));
         (ZW_GPIOA_PORT)->CFGLR |= (GPIO_Speed_10MHz | GPIO_CNF_OUT_PP)<<(4*ZW_GPIOA);
      } else {
			(ZW_GPIOA_PORT)->CFGLR &= ~(0xf<<(4*ZW_GPIOA));
         (ZW_GPIOA_PORT)->CFGLR |= (GPIO_Speed_10MHz | GPIO_CNF_IN_FLOATING)<<(4*ZW_GPIOA);
		}

  } if (addr == 0x11) {

      printf(" SET GPIO %2x = %2x\r\n", addr, data);

      if ((data & 0x80) == 0x80)
         (ZW_GPIOH_PORT)->BSHR = (1 << ZW_GPIOH);
      else
         (ZW_GPIOH_PORT)->BSHR = (1 << (16 + ZW_GPIOH));

      if ((data & 0x02) == 0x02)
         (ZW_GPIOB_PORT)->BSHR = (1 << ZW_GPIOB);
      else
         (ZW_GPIOB_PORT)->BSHR = (1 << (16 + ZW_GPIOB));

      if ((data & 0x01) == 0x01)
         (ZW_GPIOA_PORT)->BSHR = (1 << ZW_GPIOA);
      else
         (ZW_GPIOA_PORT)->BSHR = (1 << (16 + ZW_GPIOA));

	}

}

int isalpha(int c) {
    // Check if c is uppercase or lowercase letter
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) {
        return 1; // True
    }
    return 0; // False
}

int isdigit(int c) {
    // Check if c is uppercase or lowercase letter
    if (c >= '0' && c <= '9') {
        return 1; // True
    }
    return 0; // False
}

int toupper(int c) {
    // If c is a lowercase letter, convert to uppercase
    if (c >= 'a' && c <= 'z') {
        return c - ('a' - 'A'); // or c - 32
    }
    return c; // Return unchanged otherwise
}
