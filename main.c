/***************************************************************************
 *   Copyright (C) 09/2007 by Olaf Rempel                                  *
 *   razzor@kopf-tisch.de                                                  *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; version 2 of the License,               *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/
#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/boot.h>
#include <avr/pgmspace.h>

#define F_CPU 8000000
#include <util/delay.h>

#define LED_RT			(1<<PORTB4)
#define LED_GN			(1<<PORTB5)

#define TWI_ADDRESS		0x21

#define APP_END			0x1C00

#define CMD_WAIT		0x00
#define CMD_GET_INFO		0x10
#define CMD_GET_SIGNATURE	0x11
#define CMD_WRITE_FLASH		0x12
#define CMD_READ_FLASH		0x13
#define CMD_WRITE_EEPROM	0x14
#define CMD_READ_EEPROM		0x15
#define CMD_BOOT_APPLICATION	0x1F

/*
 * LED on PORTB4 blinks with 20Hz (while bootloader is running)
 * LED on PORTB5 blinks on TWI activity
 *
 * bootloader twi-protocol:
 * - get info about bootloader:
 *   SLA+W, 0x10, SLA+R, {16 bytes}, STO
 *
 * - get signature bytes:
 *   SLA+W, 0x11, SLA+R, {4 bytes}, STO
 *
 * - write one flash page (64bytes on mega8)
 *   SLA+W, 0x12, addrh, addrl, {64 bytes}, STO
 *
 * - read one (or more) flash bytes
 *   SLA+W, 0x13, addrh, addrl, SLA+R, {* bytes}, STO
 *
 * - write one (or more) eeprom bytes
 *   SLA+W, 0x14, addrh, addrl, {* bytes}, STO
 *
 * - read one (or more) eeprom bytes
 *   SLA+W, 0x15, addrh, addrl, SLA+R, {* bytes}, STO
 *
 * - boot application
 *   SLA+W, 0x1F, STO
 */

const static uint8_t info[16] = "TWIBOOT m8-v1.0";
const static uint8_t signature[4] = { 0x1E, 0x93, 0x07, 0x00 };

/* wait 40 * 25ms = 1s */
volatile static uint8_t boot_timeout = 40;
volatile static uint8_t cmd = CMD_WAIT;

/* flash buffer */
static uint8_t buf[SPM_PAGESIZE];
static uint16_t addr;

void write_flash_page(void)
{
	uint16_t pagestart = addr;
	uint8_t size = SPM_PAGESIZE;
	uint8_t *p = buf;

	if (pagestart >= APP_END)
		return;

	boot_page_erase(pagestart);
	boot_spm_busy_wait();

	do {
		uint16_t data = *p++;
		data |= *p++ << 8;
		boot_page_fill(addr, data);

		addr += 2;
		size -= 2;
	} while (size);

	boot_page_write(pagestart);
	boot_spm_busy_wait();
	boot_rww_enable();
}

void write_eeprom_byte(uint8_t val)
{
	EEARL = addr;
	EEARH = (addr >> 8);
	EEDR = val;
	addr++;

	EECR |= (1<<EEMWE);
	EECR |= (1<<EEWE);
	eeprom_busy_wait();
}

ISR(TWI_vect)
{
	static uint8_t bcnt = 0;

	switch (TWSR & 0xF8) {
	/* SLA+W received, ACK returned -> receive data and ACK */
	case 0x60:
		bcnt = 0;
 		PORTB |= LED_RT;
		TWCR |= (1<<TWINT) | (1<<TWEA);
		break;

	/* prev. SLA+W, data received, ACK returned -> receive data and ACK */
	case 0x80:
		if (bcnt == 0) {
			cmd = TWDR;
			switch (cmd) {
			case CMD_GET_INFO:
			case CMD_GET_SIGNATURE:
			case CMD_WRITE_FLASH:
			case CMD_READ_FLASH:
				/* abort countdown */
				boot_timeout = 0;

			case CMD_BOOT_APPLICATION:
				bcnt++;
				break;

			default:
				cmd = CMD_WAIT;
				bcnt = 0;
				break;
			}

		} else if (bcnt == 1) {
			addr = (TWDR << 8);
			bcnt++;

		} else if (bcnt == 2) {
			addr |= TWDR;
			bcnt++;

		} else if (bcnt >= 3 && cmd == CMD_WRITE_FLASH) {
			buf[bcnt -3] = TWDR;
			if (bcnt < sizeof(buf) +2) {
				bcnt++;

			} else {
				write_flash_page();
				bcnt = 0;
			}

		} else if (bcnt >= 3 && cmd == CMD_WRITE_EEPROM) {
			write_eeprom_byte(TWDR);
			bcnt++;
		}

		TWCR |= (1<<TWINT) | (1<<TWEA);
		break;

	/* SLA+R received, ACK returned -> send data */
	case 0xA8:
		bcnt = 0;
		PORTB |= LED_RT;

	/* prev. SLA+R, data sent, ACK returned -> send data */
	case 0xB8:
		switch (cmd) {
		case CMD_GET_INFO:
			TWDR = info[bcnt++];
			bcnt %= sizeof(info);
			break;

		case CMD_GET_SIGNATURE:
			TWDR = signature[bcnt++];
			bcnt %= sizeof(signature);
			break;

		case CMD_READ_FLASH:
			TWDR = pgm_read_byte_near(addr++);
			break;

		case CMD_READ_EEPROM:
			EEARL = addr;
			EEARH = (addr >> 8);
			EECR |= (1<<EERE);
			addr++;
			TWDR = EEDR;
			break;

		default:
			TWDR = 0xFF;
			break;
		}

		TWCR |= (1<<TWINT) | (1<<TWEA);
		break;

	/* STOP or repeated START */
	case 0xA0:
	/* data sent, NACK returned */
	case 0xC0:
		PORTB &= ~LED_RT;
		TWCR |= (1<<TWINT) | (1<<TWEA);
		break;

	/* illegal state -> reset hardware */
	case 0xF8:
		TWCR |= (1<<TWINT) | (1<<TWSTO) | (1<<TWEA);
		break;
	}
}

ISR(TIMER0_OVF_vect)
{
	/* come back in 25ms (@8MHz) */
	TCNT0 = 0xFF - 195;

	/* blink LED while running */
	PORTB ^= LED_GN;

	/* count down for app-boot */
	if (boot_timeout > 1)
		boot_timeout--;

	/* trigger app-boot */
	else if (boot_timeout == 1)
		cmd = CMD_BOOT_APPLICATION;
}

static void (*jump_to_app)(void) = 0x0000;

int main(void)
{
	DDRB = LED_GN | LED_RT;
	PORTB = LED_GN;

	/* move tnterrupt-vectors to bootloader */
	GICR = (1<<IVCE);
	GICR = (1<<IVSEL);

	/* timer0: running with F_CPU/1024 */
	TCCR0 = (1<<CS02) | (1<<CS00);

	/* enable timer0 OVF interrupt */
	TIMSK |= (1<<TOIE0);

	/* TWI init: set address, auto ACKs with interrupts */
	TWAR = (TWI_ADDRESS<<1);
	TWCR = (1<<TWEA) | (1<<TWEN) | (1<<TWIE);

	sei();
	while (cmd != CMD_BOOT_APPLICATION);
	cli();

	/* Disable TWI but keep address! */
	TWCR = 0x00;

	/* disable timer0 */
	TIMSK = 0x00;
	TCCR0 = 0x00;

	/* move interrupt vectors back to application */
	GICR = (1<<IVCE);
	GICR = 0x00;

	PORTB = 0x00;

	_delay_ms(25);
	_delay_ms(25);
	_delay_ms(25);
	_delay_ms(25);

	jump_to_app();
	return 0;
}
