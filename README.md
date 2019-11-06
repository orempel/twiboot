# twiboot - a TWI / I2C bootloader for AVR MCUs ##
twiboot is a simple/small bootloader for AVR MCUs with integrated TWI peripheral written in C.
It was originally created to update I2C controlled BLMCs (Brushless Motor Controller) without an AVR ISP adapter.

twiboot acts as a slave device on a TWI/I2C bus and allows reading/writing of the internal flash memory.
As a compile time option twiboot also allows reading/writing of the whole internal EEPROM memory.
The bootloader is not able to update itself (only application flash memory region accessible).

Currently the following AVR MCUs are supported:

AVR MCU | Flash bytes used (.text + .data) | Bootloader region size
--- | --- | ---
atmega8 | 900 (0x384) | 512 words
atmega88 | 950 (0x3B6) | 512 words
atmega168 | 1002 (0x3EA) | 512 words
atmega328p | 1002 (0x3EA) | 512 words
(Compiled on Ubuntu 18.04 LTS (gcc 5.4.0 / avr-libc 2.0.0) with EEPROM and LED support)


## Operation ##
twiboot is installed in the bootloader memory region and executed directly after reset (BOOTRST fuse is programmed).

While running, twiboot configures the TWI peripheral as slave device and waits for valid protocol messages
directed to its address on the TWI/I2C bus. The slave address is configured during compile time of twiboot.
When receiving no messages for 1000ms after reset, the bootloader exits and executes the main application at address 0x0000.

A TWI/I2C master can use the protocol to
- abort the boot timeout
- query information about the device (bootloader version, AVR signature bytes, flash/eeprom size, flash page size)
- read internal flash / eeprom memory (byte wise)
- write the internal flash (page wise)
- write the internal eeprom (byte wise)
- exit the bootloader and start the application

As a compile time option twiboot can output its state with two LEDs.
One LED will flash with a frequency of 20Hz while twiboot is active (including boot wait time).
A second LED will flash when the bootloader is addressed on the TWI/I2C bus.


## Build and install twiboot ##
twiboot uses gcc, avr-libc and GNU Make for building, avrdude is used for flashing the MCU.
The build and install procedures are only tested under linux.

The selection of the target MCU and the programming interface can be found in the Makefile,
TWI/I2C slave address and optional components (EEPROM / LED support) are configured
in the main.c source.

To build twiboot for the selected target:
``` shell
$ make
```

To install (flash download) twiboot with avrdude on the target:
``` shell
$ make install
```

Set AVR fuses with avrdude on the target (internal RC-Osz, enable BOD, enable BOOTRST):
``` shell
$ make fuses
```


## TWI/I2C Protocol ##
A TWI/I2C master can use the following protocol for accessing the bootloader.

Function | TWI/I2C data | Comment
--- | --- | ---
Abort boot timeout | **SLA+W**, 0x00, **STO** |
Show bootloader version | **SLA+W**, 0x01, **SLA+R**, {16 bytes}, **STO** | ASCII, not null terminated
Start application | **SLA+W**, 0x01, 0x80, **STO** |
Read chip info | **SLA+W**, 0x02, 0x00, 0x00, 0x00, **SLA+R**, {8 bytes}, **STO** | 3byte signature, 1byte page size, 2byte flash size, 2byte eeprom size
Read 1+ flash bytes | **SLA+W**, 0x02, 0x01, addrh, addrl, **SLA+R**, {* bytes}, **STO** |
Read 1+ eeprom bytes | **SLA+W**, 0x02, 0x02, addrh, addrl, **SLA+R**, {* bytes}, **STO** |
Write one flash page | **SLA+W**, 0x02, 0x01, addrh, addrl, {* bytes}, **STO** | page size as indicated in chip info
Write 1+ eeprom bytes | **SLA+W**, 0x02, 0x02, addrh, addrl, {* bytes}, **STO** |

**SLA+R** means Start Condition, Slave Address, Read Access

**SLA+W** means Start Condition, Slave Address, Write Access

**STO** means Stop Condition

The multiboot_tool repository contains a simple linux application that uses
this protocol to access the bootloader over linux i2c device.


## Development ##
Issue reports, feature requests, patches or simply success stories are much appreciated.


## Roadmap ##
Some ideas that I want to investigate / implement in twiboot:
- [ ] do not use interrupts and remove vector table to save flash space
- [ ] find a way to not rely on TWI clock stretching during write access
- [ ] support AVR TINYs (USI peripheral, no bootloader fuse, no Read-While-Write flash)
