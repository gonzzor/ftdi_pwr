#include <stdio.h>
#include <libgen.h> /* for basename() */
#include <string.h>
#include <ftdi.h>

#define USB_VENDOR_ID 0x0403
#define USB_PRODUCT_ID 0x6001
#define USB_PRODUCT "EVAL232 Board USB <-> Serial"

/* CBUSx for RESET and POWER */
#define RESET_PIN 2
#define POWER_PIN 3

enum command {
	CHECK,
	RESET,
	POWER,
	LONGPOWER
};

void print_cbus(const struct ftdi_eeprom *eeprom)
{
	char *cbus_mux[] = {"TXDEN", "PWREN", "RXLED", "TXLED", "TX+RXLED",
		"SLEEP","CLK48","CLK24","CLK12","CLK6",
		"IOMODE","BB_WR","BB_RD"};

	for (int i = 0; i < 5; i++) {
		printf("CBUS%d: %2d(%s)\n", i, eeprom->cbus_function[i],
				cbus_mux[eeprom->cbus_function[i]]);
	}
}

int check_eeprom(struct ftdi_context *ftdi)
{
	char buf[FTDI_DEFAULT_EEPROM_SIZE];
	int ret;
	struct ftdi_eeprom eeprom;

	ret = ftdi_read_eeprom(ftdi, buf);
	if (ret < 0) {
		fprintf(stderr, "Unable to read eeprom: %d (%s)\n", ret,
				ftdi_get_error_string(ftdi));
		goto err_out;
	}

	ret = ftdi_eeprom_decode(&eeprom, buf, FTDI_DEFAULT_EEPROM_SIZE);
	if (ret < 0) {
		fprintf(stderr, "Unable to decode eeprom: %d (%s)\n", ret,
				ftdi_get_error_string(ftdi));
		goto err_out;
	}

	print_cbus(&eeprom);

	if (eeprom.cbus_function[2] != CBUS_IOMODE ||
			eeprom.cbus_function[3] != CBUS_IOMODE) {
		printf("CBUS2 or CBUS3 is wrong, fixing...\n");
		eeprom.cbus_function[2] = CBUS_IOMODE;
		eeprom.cbus_function[3] = CBUS_IOMODE;

		/* ftdi_eeprom_decode() doesn't set size correct */
		if (eeprom.size == 0)
			eeprom.size = FTDI_DEFAULT_EEPROM_SIZE;

		ret = ftdi_eeprom_build(&eeprom, buf);
		if (ret < 0) {
			fprintf(stderr, "Unable to build eeprom: %d (%s)\n",
					ret, ftdi_get_error_string(ftdi));
			goto err_out;
		}

		ret = ftdi_write_eeprom(ftdi, buf);
		if (ret < 0) {
			fprintf(stderr, "Unable to write eeprom: %d (%s)\n",
					ret, ftdi_get_error_string(ftdi));
			goto err_out;
		}

		print_cbus(&eeprom);
	}

	ftdi_eeprom_free(&eeprom);

	return 0;

err_out:
	ftdi_eeprom_free(&eeprom);

	return -1;
}

/* Toggle CBUS pin high for time seconds */
int toggle_cbus(struct ftdi_context *ftdi, int pin, int time)
{
	int ret;
	unsigned char bitmask;

	/*
	 * BITMASK
	 * CBUS Bits
	 * 3210 3210
	 * xxxx xxxx
	 * |    |------ Output Control 0->LO, 1->HI
	 * |----------- Input/Output   0->Input, 1->Output
	 */
	bitmask = 0x11 << pin;
	ret = ftdi_set_bitmode(ftdi, bitmask, BITMODE_CBUS);
	if (ret < 0) {
		fprintf(stderr, "Unable to set bitmode %#.2x: %d (%s)\n", ret,
				ftdi_get_error_string(ftdi));
		ftdi_free(ftdi);
		return -1;
	}

	usleep(time * 1000000);

	bitmask = 0x10 << pin;
	ret = ftdi_set_bitmode(ftdi, bitmask, BITMODE_CBUS);
	if (ret < 0) {
		fprintf(stderr, "Unable to set bitmode %#.2x: %d (%s)\n", ret,
				ftdi_get_error_string(ftdi));
		ftdi_free(ftdi);
		return -1;
	}

	return 0;
}

int reset(struct ftdi_context *ftdi)
{
	printf("Toggle reset for 1 second\n");
	return toggle_cbus(ftdi, RESET_PIN, 1);
}

int power(struct ftdi_context *ftdi)
{
	printf("Toggle power for 1 second\n");
	return toggle_cbus(ftdi, POWER_PIN, 1);
}

int longpower(struct ftdi_context *ftdi)
{
	printf("Toggle power for 5 seconds\n");
	return toggle_cbus(ftdi, POWER_PIN, 5);
}

int doit(enum command cmd)
{
	struct ftdi_context *ftdi;
	int ret;

	ftdi = ftdi_new();
	if (ftdi == NULL) {
		fprintf(stderr, "Failed to allocate ftdi structure\n");
		return -1;
	}

	printf("Searching for device 0x%.4x 0x%.4x, %s\n", USB_VENDOR_ID, USB_PRODUCT_ID, USB_PRODUCT);
	ret = ftdi_usb_open_desc(ftdi, USB_VENDOR_ID, USB_PRODUCT_ID, USB_PRODUCT, NULL);
	if (ret < 0) {
		fprintf(stderr, "Unable to find ftdi device: %d (%s)\n", ret,
				ftdi_get_error_string(ftdi));
		goto err_out;
	}

	printf("Checking eeprom\n");
	if (check_eeprom(ftdi))
		goto err_out;

	switch (cmd) {
		case CHECK:
			break;
		case RESET:
			if (reset(ftdi))
				goto err_out;
			break;
		case POWER:
			if (power(ftdi))
				goto err_out;
			break;
		case LONGPOWER:
			if (longpower(ftdi))
				goto err_out;
			break;
		default:
			fprintf(stderr, "Wrong cmd in doit()\n");
			goto err_out;
			break;
	}

	ret = ftdi_usb_close(ftdi);
	if (ret < 0) {
		fprintf(stderr, "Unable to close ftdi device: %d (%s)\n", ret,
				ftdi_get_error_string(ftdi));
		goto err_out;
	}

out:
	ftdi_free(ftdi);
	return 0;

err_out:
	ftdi_free(ftdi);
	return -1;
}

void usage(char *argv0)
{
	printf("Usage: %s check|reset|power|longpower\n", basename(argv0));
}

int main(int argc, char *argv[])
{
	enum command cmd;

	if (argc != 2) {
		usage(argv[0]);
		return -1;
	}

	if (strcmp(argv[1], "check") == 0) {
		cmd = CHECK;
	} else if (strcmp(argv[1], "reset") == 0) {
		cmd = RESET;
	} else if (strcmp(argv[1], "power") == 0) {
		cmd = POWER;
	} else if (strcmp(argv[1], "longpower") == 0) {
		cmd = LONGPOWER;
	} else {
		usage(argv[0]);
		return -1;
	}

	return doit(cmd);
}
