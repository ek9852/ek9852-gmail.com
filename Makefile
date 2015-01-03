CFLAGS += -Wall $(shell pkg-config --cflags libelf)
LDLIBS += $(shell pkg-config --libs libelf)

all: elf2rprc

elf2rprc: main.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

clean:
	rm -f *.o elf2rprc

.PHONY: all clean

