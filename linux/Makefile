TARGET  = twiboot
TARGET2 = mpmboot

CFLAGS = -Wall -Wno-unused-result -O2 -MMD -MP -MF $(*F).d

# ------

SRC := $(wildcard *.c)

all: $(TARGET)

$(TARGET): $(SRC:.c=.o)
	@echo " Linking file:  $@"
	@$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS) > /dev/null
	@ln -sf $@ $(TARGET2)

%.o: %.c
	@echo " Building file: $<"
	@$(CC) -c $(CFLAGS) $< -o $@

clean:
	rm -rf $(TARGET) $(TARGET2) *.o *.d

-include $(shell find . -name \*.d 2> /dev/null)
