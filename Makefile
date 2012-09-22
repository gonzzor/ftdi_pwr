LDLIBS=-lftdi
CFLAGS=-std=gnu99 -g -O2

all: ftdi_pwr

clean:
	rm -f ftdi_pwr
