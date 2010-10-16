TARGET = twiboot

CFLAGS = -Wall -Wno-unused-result -O2 -MMD -MP -MF $(*F).d

# ------

SRC := $(wildcard *.c)

all: $(TARGET)

$(TARGET): $(SRC:.c=.o)
	@echo " Linking file:  $@"
	@$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS) > /dev/null

%.o: %.c
	@echo " Building file: $<"
	@$(CC) -c $(CFLAGS) $< -o $@

clean:
	rm -rf $(TARGET) *.o *.d

-include $(shell find . -name \*.d 2> /dev/null)
