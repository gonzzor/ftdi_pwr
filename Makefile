LDLIBS = -lftdi
CFLAGS += -std=gnu99

all: ftdi_pwr

clean:
	rm -f ftdi_pwr
