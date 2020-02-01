/***************************************************************************
 *   Copyright (C) 11/2019 by Olaf Rempel                                  *
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

#define VERSION_STRING      "TWIBOOT v2.1"
#define EEPROM_SUPPORT      1
#define LED_SUPPORT         1
#define USE_CLOCKSTRETCH    0

#define F_CPU               8000000ULL
#define TIMER_DIVISOR       1024
#define TIMER_IRQFREQ_MS    25
#define TIMEOUT_MS          1000

#define TIMER_MSEC2TICKS(x)     ((x * F_CPU) / (TIMER_DIVISOR * 1000ULL))
#define TIMER_MSEC2IRQCNT(x)    (x / TIMER_IRQFREQ_MS)

#if LED_SUPPORT
#define LED_INIT()          DDRB = ((1<<PORTB4) | (1<<PORTB5))
#define LED_RT_ON()         PORTB |= (1<<PORTB4)
#define LED_RT_OFF()        PORTB &= ~(1<<PORTB4)
#define LED_GN_ON()         PORTB |= (1<<PORTB5)
#define LED_GN_OFF()        PORTB &= ~(1<<PORTB5)
#define LED_GN_TOGGLE()     PORTB ^= (1<<PORTB5)
#define LED_OFF()           PORTB = 0x00
#else
#define LED_INIT()
#define LED_RT_ON()
#define LED_RT_OFF()
#define LED_GN_ON()
#define LED_GN_OFF()
#define LED_GN_TOGGLE()
#define LED_OFF()
#endif

#ifndef TWI_ADDRESS
#define TWI_ADDRESS         0x29
#endif

/* SLA+R */
#define CMD_WAIT                0x00
#define CMD_READ_VERSION        0x01
#define CMD_ACCESS_MEMORY       0x02
/* internal mappings */
#define CMD_ACCESS_CHIPINFO     (0x10 | CMD_ACCESS_MEMORY)
#define CMD_ACCESS_FLASH        (0x20 | CMD_ACCESS_MEMORY)
#define CMD_ACCESS_EEPROM       (0x30 | CMD_ACCESS_MEMORY)
#define CMD_WRITE_FLASH_PAGE    (0x40 | CMD_ACCESS_MEMORY)
#define CMD_WRITE_EEPROM_PAGE   (0x50 | CMD_ACCESS_MEMORY)

/* SLA+W */
#define CMD_SWITCH_APPLICATION  CMD_READ_VERSION
/* internal mappings */
#define CMD_BOOT_BOOTLOADER     (0x10 | CMD_SWITCH_APPLICATION) /* only in APP */
#define CMD_BOOT_APPLICATION    (0x20 | CMD_SWITCH_APPLICATION)

/* CMD_SWITCH_APPLICATION parameter */
#define BOOTTYPE_BOOTLOADER     0x00    /* only in APP */
#define BOOTTYPE_APPLICATION    0x80

/* CMD_{READ|WRITE}_* parameter */
#define MEMTYPE_CHIPINFO        0x00
#define MEMTYPE_FLASH           0x01
#define MEMTYPE_EEPROM          0x02

/*
 * LED_GN flashes with 20Hz (while bootloader is running)
 * LED_RT flashes on TWI activity
 *
 * bootloader twi-protocol:
 * - abort boot timeout:
 *   SLA+W, 0x00, STO
 *
 * - show bootloader version
 *   SLA+W, 0x01, SLA+R, {16 bytes}, STO
 *
 * - start application
 *   SLA+W, 0x01, 0x80, STO
 *
 * - read chip info: 3byte signature, 1byte page size, 2byte flash size, 2byte eeprom size
 *   SLA+W, 0x02, 0x00, 0x00, 0x00, SLA+R, {8 bytes}, STO
 *
 * - read one (or more) flash bytes
 *   SLA+W, 0x02, 0x01, addrh, addrl, SLA+R, {* bytes}, STO
 *
 * - read one (or more) eeprom bytes
 *   SLA+W, 0x02, 0x02, addrh, addrl, SLA+R, {* bytes}, STO
 *
 * - write one flash page
 *   SLA+W, 0x02, 0x01, addrh, addrl, {* bytes}, STO
 *
 * - write one (or more) eeprom bytes
 *   SLA+W, 0x02, 0x02, addrh, addrl, {* bytes}, STO
 */

const static uint8_t info[16] = VERSION_STRING;
const static uint8_t chipinfo[8] = {
    SIGNATURE_0, SIGNATURE_1, SIGNATURE_2,
    SPM_PAGESIZE,

    (BOOTLOADER_START >> 8) & 0xFF,
    BOOTLOADER_START & 0xFF,

#if (EEPROM_SUPPORT)
    ((E2END +1) >> 8 & 0xFF),
    (E2END +1) & 0xFF
#else
    0x00, 0x00
#endif
};

static uint8_t boot_timeout = TIMER_MSEC2IRQCNT(TIMEOUT_MS);
static uint8_t cmd = CMD_WAIT;

/* flash buffer */
static uint8_t buf[SPM_PAGESIZE];
static uint16_t addr;

/* *************************************************************************
 * write_flash_page
 * ************************************************************************* */
static void write_flash_page(void)
{
    uint16_t pagestart = addr;
    uint8_t size = SPM_PAGESIZE;
    uint8_t *p = buf;

    if (pagestart < BOOTLOADER_START)
    {
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
} /* write_flash_page */


#if (EEPROM_SUPPORT)
/* *************************************************************************
 * read_eeprom_byte
 * ************************************************************************* */
static uint8_t read_eeprom_byte(uint16_t address)
{
    EEARL = address;
    EEARH = (address >> 8);
    EECR |= (1<<EERE);

    return EEDR;
} /* read_eeprom_byte */


/* *************************************************************************
 * write_eeprom_byte
 * ************************************************************************* */
static void write_eeprom_byte(uint8_t val)
{
    EEARL = addr;
    EEARH = (addr >> 8);
    EEDR = val;
    addr++;

#if defined (EEWE)
    EECR |= (1<<EEMWE);
    EECR |= (1<<EEWE);
#elif defined (EEPE)
    EECR |= (1<<EEMPE);
    EECR |= (1<<EEPE);
#else
#error "EEWE/EEPE not defined"
#endif

    eeprom_busy_wait();
} /* write_eeprom_byte */


#if (USE_CLOCKSTRETCH == 0)
/* *************************************************************************
 * write_eeprom_buffer
 * ************************************************************************* */
static void write_eeprom_buffer(uint8_t size)
{
    uint8_t *p = buf;

    while (size--)
    {
        write_eeprom_byte(*p++);
    }
} /* write_eeprom_buffer */
#endif /* (USE_CLOCKSTRETCH == 0) */
#endif /* EEPROM_SUPPORT */


/* *************************************************************************
 * TWI_data_write
 * ************************************************************************* */
static uint8_t TWI_data_write(uint8_t bcnt, uint8_t data)
{
    uint8_t ack = 0x01;

    switch (bcnt)
    {
        case 0:
            switch (data)
            {
                case CMD_SWITCH_APPLICATION:
                case CMD_ACCESS_MEMORY:
                    /* no break */

                case CMD_WAIT:
                    /* abort countdown */
                    boot_timeout = 0;
                    cmd = data;
                    break;

                default:
                    /* boot app now */
                    cmd = CMD_BOOT_APPLICATION;
                    ack = 0x00;
                    break;
            }
            break;

        case 1:
            switch (cmd)
            {
                case CMD_SWITCH_APPLICATION:
                    if (data == BOOTTYPE_APPLICATION)
                    {
                        cmd = CMD_BOOT_APPLICATION;
                    }

                    ack = 0x00;
                    break;

                case CMD_ACCESS_MEMORY:
                    if (data == MEMTYPE_CHIPINFO)
                    {
                        cmd = CMD_ACCESS_CHIPINFO;
                    }
                    else if (data == MEMTYPE_FLASH)
                    {
                        cmd = CMD_ACCESS_FLASH;
                    }
#if (EEPROM_SUPPORT)
                    else if (data == MEMTYPE_EEPROM)
                    {
                        cmd = CMD_ACCESS_EEPROM;
                    }
#endif /* (EEPROM_SUPPORT) */
                    else
                    {
                        ack = 0x00;
                    }
                    break;

                default:
                    ack = 0x00;
                    break;
            }
            break;

        case 2:
        case 3:
            addr <<= 8;
            addr |= data;
            break;

        default:
            switch (cmd)
            {
#if (EEPROM_SUPPORT)
#if (USE_CLOCKSTRETCH)
                case CMD_ACCESS_EEPROM:
                    write_eeprom_byte(data);
                    break;
#else
                case CMD_ACCESS_EEPROM:
                    cmd = CMD_WRITE_EEPROM_PAGE;
                    /* fall through */

                case CMD_WRITE_EEPROM_PAGE:
#endif /* (USE_CLOCKSTRETCH) */
#endif /* (EEPROM_SUPPORT) */
                case CMD_ACCESS_FLASH:
                {
                    uint8_t pos = bcnt -4;

                    buf[pos] = data;
                    if (pos >= (sizeof(buf) -2))
                    {
                        ack = 0x00;
                    }

                    if ((cmd == CMD_ACCESS_FLASH) &&
                        (pos >= (sizeof(buf) -1))
                       )
                    {
#if (USE_CLOCKSTRETCH)
                        write_flash_page();
#else
                        cmd = CMD_WRITE_FLASH_PAGE;
#endif
                    }
                    break;
                }

                default:
                    ack = 0x00;
                    break;
            }
            break;
    }

    return ack;
} /* TWI_data_write */


/* *************************************************************************
 * TWI_data_read
 * ************************************************************************* */
static uint8_t TWI_data_read(uint8_t bcnt)
{
    uint8_t data;

    switch (cmd)
    {
        case CMD_READ_VERSION:
            bcnt %= sizeof(info);
            data = info[bcnt];
            break;

        case CMD_ACCESS_CHIPINFO:
            bcnt %= sizeof(chipinfo);
            data = chipinfo[bcnt];
            break;

        case CMD_ACCESS_FLASH:
            data = pgm_read_byte_near(addr++);
            break;

#if (EEPROM_SUPPORT)
        case CMD_ACCESS_EEPROM:
            data = read_eeprom_byte(addr++);
            break;
#endif /* (EEPROM_SUPPORT) */

        default:
            data = 0xFF;
            break;
    }

    return data;
} /* TWI_data_read */


/* *************************************************************************
 * TWI_vect
 * ************************************************************************* */
static void TWI_vect(void)
{
    static uint8_t bcnt;
    uint8_t control = TWCR;

    switch (TWSR & 0xF8)
    {
        /* SLA+W received, ACK returned -> receive data and ACK */
        case 0x60:
            bcnt = 0;
            LED_RT_ON();
            break;

        /* prev. SLA+W, data received, ACK returned -> receive data and ACK */
        case 0x80:
            if (TWI_data_write(bcnt++, TWDR) == 0x00)
            {
                control &= ~(1<<TWEA);
            }
            break;

        /* SLA+R received, ACK returned -> send data */
        case 0xA8:
            bcnt = 0;
            LED_RT_ON();

        /* prev. SLA+R, data sent, ACK returned -> send data */
        case 0xB8:
            TWDR = TWI_data_read(bcnt++);
            break;

        /* prev. SLA+W, data received, NACK returned -> IDLE */
        case 0x88:
            TWI_data_write(bcnt++, TWDR);
            /* fall through */

        /* STOP or repeated START -> IDLE */
        case 0xA0:
#if (USE_CLOCKSTRETCH == 0)
            if ((cmd == CMD_WRITE_FLASH_PAGE)
#if (EEPROM_SUPPORT)
                || (cmd == CMD_WRITE_EEPROM_PAGE)
#endif
               )
            {
                /* disable ACK for now, re-enable after page write */
                control &= ~(1<<TWEA);
                TWCR = (1<<TWINT) | control;

#if (EEPROM_SUPPORT)
                if (cmd == CMD_WRITE_EEPROM_PAGE)
                {
                    write_eeprom_buffer(bcnt -4);
                }
                else
#endif /* (EEPROM_SUPPORT) */
                {
                    write_flash_page();
                }
            }
#endif /* (USE_CLOCKSTRETCH) */

            bcnt = 0;
            /* fall through */

        /* prev. SLA+R, data sent, NACK returned -> IDLE */
        case 0xC0:
            LED_RT_OFF();
            control |= (1<<TWEA);
            break;

        /* illegal state(s) -> reset hardware */
        default:
            control |= (1<<TWSTO);
            break;
    }

    TWCR = (1<<TWINT) | control;
} /* TWI_vect */


/* *************************************************************************
 * TIMER0_OVF_vect
 * ************************************************************************* */
static void TIMER0_OVF_vect(void)
{
    /* restart timer */
    TCNT0 = 0xFF - TIMER_MSEC2TICKS(TIMER_IRQFREQ_MS);

    /* blink LED while running */
    LED_GN_TOGGLE();

    /* count down for app-boot */
    if (boot_timeout > 1)
    {
        boot_timeout--;
    }
    else if (boot_timeout == 1)
    {
        /* trigger app-boot */
        cmd = CMD_BOOT_APPLICATION;
    }
} /* TIMER0_OVF_vect */


static void (*jump_to_app)(void) __attribute__ ((noreturn)) = 0x0000;


/* *************************************************************************
 * init1
 * ************************************************************************* */
void init1(void) __attribute__((naked, section(".init1")));
void init1(void)
{
  /* make sure r1 is 0x00 */
  asm volatile ("clr __zero_reg__");

  /* on some MCUs the stack pointer defaults NOT to RAMEND */
#if defined(__AVR_ATmega8__) || defined(__AVR_ATmega8515__) || \
    defined(__AVR_ATmega8535__) || defined (__AVR_ATmega16__) || \
    defined (__AVR_ATmega32__) || defined (__AVR_ATmega64__)  || \
    defined (__AVR_ATmega128__) || defined (__AVR_ATmega162__)
  SP = RAMEND;
#endif
} /* init1 */


/*
 * For newer devices the watchdog timer remains active even after a
 * system reset. So disable it as soon as possible.
 * automagically called on startup
 */
#if defined (__AVR_ATmega88__) || defined (__AVR_ATmega168__) || \
    defined (__AVR_ATmega328P__)
/* *************************************************************************
 * disable_wdt_timer
 * ************************************************************************* */
void disable_wdt_timer(void) __attribute__((naked, section(".init3")));
void disable_wdt_timer(void)
{
    MCUSR = 0;
    WDTCSR = (1<<WDCE) | (1<<WDE);
    WDTCSR = (0<<WDE);
} /* disable_wdt_timer */
#endif


/* *************************************************************************
 * main
 * ************************************************************************* */
int main(void) __attribute__ ((OS_main, section (".init9")));
int main(void)
{
    LED_INIT();
    LED_GN_ON();

    /* timer0: running with F_CPU/1024 */
#if defined (TCCR0)
    TCCR0 = (1<<CS02) | (1<<CS00);
#elif defined (TCCR0B)
    TCCR0B = (1<<CS02) | (1<<CS00);
#else
#error "TCCR0(B) not defined"
#endif

    /* TWI init: set address, auto ACKs */
    TWAR = (TWI_ADDRESS<<1);
    TWCR = (1<<TWEA) | (1<<TWEN);

    while (cmd != CMD_BOOT_APPLICATION)
    {
        if (TWCR & (1<<TWINT))
        {
            TWI_vect();
        }

#if defined (TIFR)
        if (TIFR & (1<<TOV0))
        {
            TIMER0_OVF_vect();
            TIFR = (1<<TOV0);
        }
#elif defined (TIFR0)
        if (TIFR0 & (1<<TOV0))
        {
            TIMER0_OVF_vect();
            TIFR0 = (1<<TOV0);
        }
#else
#error "TIFR(0) not defined"
#endif
    }

    /* Disable TWI but keep address! */
    TWCR = 0x00;

    /* disable timer0 */
#if defined (TCCR0)
    TCCR0 = 0x00;
#elif defined (TCCR0B)
    TCCR0B = 0x00;
#else
#error "TCCR0(B) not defined"
#endif

    LED_OFF();

    uint16_t wait = 0x0000;
    do {
        __asm volatile ("nop");
    } while (--wait);

    jump_to_app();
} /* main */
