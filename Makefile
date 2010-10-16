PRG            = twiboot
OBJ            = main.o
MCU_TARGET     = atmega88
OPTIMIZE       = -Os

ifeq ($(MCU_TARGET), atmega8)
BOOTLOADER_START=0x1C00
AVRDUDE_MCU=m8
endif
ifeq ($(MCU_TARGET), atmega88)
BOOTLOADER_START=0x1C00
AVRDUDE_MCU=m88
endif
ifeq ($(MCU_TARGET), atmega168)
BOOTLOADER_START=0x3C00
AVRDUDE_MCU=m168
endif

DEFS           = -DAPP_END=$(BOOTLOADER_START)
LIBS           =

# Override is only needed by avr-lib build system.
override CFLAGS        = -g -Wall $(OPTIMIZE) -mmcu=$(MCU_TARGET) $(DEFS)
override LDFLAGS       = -Wl,-Map,$(PRG).map,--section-start=.text=$(BOOTLOADER_START)

CC             = avr-gcc
OBJCOPY        = avr-objcopy
OBJDUMP        = avr-objdump
SIZE           = avr-size

all: $(PRG).elf lst text
	$(SIZE) -x -A $(PRG).elf

$(PRG).elf: $(OBJ)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LIBS)

%.o: %.c $(MAKEFILE_LIST)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf *.o $(PRG).lst $(PRG).map $(PRG).elf $(PRG).hex $(PRG).bin

lst:  $(PRG).lst

%.lst: %.elf
	$(OBJDUMP) -h -S $< > $@

text: hex bin

hex:  $(PRG).hex
bin:  $(PRG).bin

%.hex: %.elf
	$(OBJCOPY) -j .text -j .data -O ihex $< $@

%.bin: %.elf
	$(OBJCOPY) -j .text -j .data -O binary $< $@

install: text
	avrdude -c dragon_isp -P usb -p $(AVRDUDE_MCU) -U flash:w:$(PRG).hex

#fuses:
#	avrdude -c dragon_isp -P usb -p $(AVRDUDE_MCU) -U lfuse:w:0xc2:m
#	avrdude -c dragon_isp -P usb -p $(AVRDUDE_MCU) -U hfuse:w:0xdd:m
#	avrdude -c dragon_isp -P usb -p $(AVRDUDE_MCU) -U efuse:w:0xfa:m
