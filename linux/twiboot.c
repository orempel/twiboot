/***************************************************************************
 *   Copyright (C) 10/2010 by Olaf Rempel                                  *
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
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <getopt.h>

#include "filedata.h"
#include "list.h"
#include "twb.h"

#define OP_MODE_READ		0x01
#define OP_MODE_WRITE		0x02
#define OP_TYPE_FLASH		0x01
#define OP_TYPE_EEPROM		0x02

struct operation {
	struct list_head list;

	char *filename;
	int flags;

	int mode;
	int memtype;
};

static LIST_HEAD(operation_list);

static struct option opts[] = {
	{"address",	1, 0, 'a'},		// -a <addr>
	{"device",	1, 0, 'd'},		// [ -d <device> ]
	{"help",	0, 0, 'h'},		// [ -h ]
	{"progress",	1, 0, 'p'},		// [ -p <0|1|2> ]
	{"read",	1, 0, 'r'},		// [ -r <flash|eeprom>:<file.hex> ]
	{"write",	1, 0, 'w'},		// [ -w <flash|eeprom>:<file.hex> ]
	{"no-verify",	0, 0, 'n'},		// [ -n ]
	{0, 0, 0, 0}
};

static struct operation * alloc_operation(const char *arg)
{
	struct operation *op = malloc(sizeof(struct operation));
	if (op == NULL) {
		perror("malloc()");
		return NULL;
	}

	if (strncmp(arg, "flash:", 6) == 0) {
		op->memtype = OP_TYPE_FLASH;
		op->filename = strdup(arg + 6);

	} else if (strncmp(arg, "eeprom:", 7) == 0) {
		op->memtype = OP_TYPE_EEPROM;
		op->filename = strdup(arg + 7);

	} else {
		fprintf(stderr, "invalid memtype: '%s'\n", arg);
		return NULL;
	}

	return op;
}

static void progress_mode1_cb(const char *msg, int pos, int size)
{
	if (pos != -1 && size != -1) {
		char stars[50];

		int i;
		int count = (pos * sizeof(stars) / size);
		for (i = 0; i < sizeof(stars); i++)
			stars[i] = (i < count) ? '*' : ' ';

		printf("%-15s: [%s] (%d)\r", msg, stars, pos);
	}

	if (pos == size)
		printf("\n");
}

static void progress_mode2_cb(const char *msg, int pos, int size)
{
	static int old_count;

	if (pos != -1 && size != -1) {
		if (pos == 0) {
			old_count = 0;
			printf("%-15s: [", msg);

		} else if (pos <=size) {
			int i;
			int count = (pos * 50 / size);
			for (i = old_count; i < count; i++)
				printf("*");

			old_count = count;

			if (pos == size) {
				printf("] (%d)\n", pos);
			}
		}
	}
}

int main(int argc, char *argv[])
{
	struct twiboot twb;
	int verify = 1, progress = 1;

	memset(&twb, 0, sizeof(struct twiboot));

	int arg = 0, code = 0, abort = 0;
	while (code != -1) {
		code = getopt_long(argc, argv, "a:d:hnp:r:w:", opts, &arg);

		switch (code) {
		case 'a':	/* address */
		{
			char *endptr;
			twb.address = strtol(optarg, &endptr, 16);
			if (*endptr != '\0' || twb.address < 0x01 || twb.address > 0x7F) {
				fprintf(stderr, "invalid address: '%s'\n", optarg);
				abort = 1;
				break;
			}
			break;
		}

		case 'd':	/* device */
			if (twb.device != NULL) {
				fprintf(stderr, "invalid device: '%s'\n", optarg);
				abort = 1;
				break;
			}

			twb.device = strdup(optarg);
			if (twb.device == NULL) {
				perror("strdup()");
				abort = 1;
				break;
			}
			break;

		case 'r':	/* read */
		{
			struct operation *op = alloc_operation(optarg);
			if (op != NULL) {
				op->mode = OP_MODE_READ;
				list_add_tail(&op->list, &operation_list);

			} else {
				abort = 1;
			}
			break;
		}

		case 'w':	/* write */
		{
			struct operation *op = alloc_operation(optarg);
			if (op != NULL) {
				op->mode = OP_MODE_WRITE;
				list_add_tail(&op->list, &operation_list);

			} else {
				abort = 1;
			}
			break;
		}

		case 'n':	/* no verify */
				verify = 0;
				break;

		case 'p':	/* progress bar mode */
		{
			if (*optarg >= '0' && *optarg <= '2') {
				progress = *optarg - '0';

			} else {
				fprintf(stderr, "invalid progress bar mode: '%s'\n", optarg);
				abort = 1;
			}
			break;
		}
				progress = 0;
				break;

		case 'h':
		case '?':	/* error */
				fprintf(stderr, "Usage: twiboot [options]\n"
					"  -a <address>                 - selects i2c address (0x01 - 0x7F)\n"
					"  -d <device>                  - selects i2c device  (default: /dev/i2c-0)\n"
					"  -r <flash|eeprom>:<file>     - reads flash/eeprom to file   (.bin | .hex | -)\n"
					"  -w <flash|eeprom>:<file>     - write flash/eeprom from file (.bin | .hex)\n"
					"  -n                           - disable verify after write\n"
					"  -p <0|1|2>                   - progress bar mode\n"
					"\n"
					"Example: twiboot -a 0x22 -w flash:blmc.hex -w flash:blmc_eeprom.hex\n"
					"\n");
				abort = 1;
				break;

		default:	/* unknown / all options parsed */
				break;
		}
	}

	if (twb.address == 0) {
		fprintf(stderr, "abort: no address given\n");
		abort = 1;
	}

	if (twb.device == NULL) {
		twb.device = strdup("/dev/i2c-0");
		if (twb.device == NULL) {
			perror("strdup()");
			abort = 1;
		}
	}

	if (!abort) {
		if (twb_open(&twb) != 0x00)
			abort = 1;
	}

	if (!abort) {
		printf("device         : %-16s (address: 0x%02X)\n", twb.device, twb.address);
		printf("version        : %-16s (sig: 0x%02x 0x%02x 0x%02x => %s)\n", twb.version, twb.signature[0], twb.signature[1], twb.signature[2], twb.chipname);
		printf("flash size     : 0x%04x / %5d   (0x%02x bytes/page)\n", twb.flashsize, twb.flashsize, twb.pagesize);
		printf("eeprom size    : 0x%04x / %5d\n", twb.eepromsize, twb.eepromsize);

		if (progress) {
			setbuf(stdout, NULL);
			twb.progress_cb = (progress == 1) ? progress_mode1_cb : progress_mode2_cb;
		}

		struct operation *op;
		list_for_each_entry(op, &operation_list, list) {
			abort = 1;
			if (op->mode == OP_MODE_READ) {
				struct databuf *dbuf;
				int result;

				if (op->memtype == OP_TYPE_FLASH) {
					twb.progress_msg = "reading flash";
					result = dbuf_alloc(&dbuf, twb.flashsize);
				} else if (op->memtype == OP_TYPE_EEPROM) {
					twb.progress_msg = "reading eeprom";
					result = dbuf_alloc(&dbuf, twb.eepromsize);
				}

				if (result != 0x00)
					break;

				result = twb_read(&twb, dbuf, op->memtype);
				if (result != 0x00) {
					fprintf(stderr, "failed to read from device\n");
					dbuf_free(dbuf);
					break;
				}

				result = file_write(op->filename, dbuf);
				if (result != 0x00) {
					fprintf(stderr, "failed to write file '%s'\n", op->filename);
					dbuf_free(dbuf);
					break;
				}

				dbuf_free(dbuf);

			} else if (op->mode == OP_MODE_WRITE) {
				struct databuf *dbuf;
				unsigned int size;
				int result;

				result = file_getsize(op->filename, &size);
				if (result != 0x00)
					break;

				result = dbuf_alloc(&dbuf, size);
				if (result != 0x00)
					break;

				result = file_read(op->filename, dbuf);
				if (result != 0x00) {
					fprintf(stderr, "failed to read file '%s'\n", op->filename);
					dbuf_free(dbuf);
					break;
				}

				if (op->memtype == OP_TYPE_FLASH) {
					twb.progress_msg = "writing flash";

					if (dbuf->length > twb.flashsize) {
						fprintf(stderr, "invalid flash size: 0x%04x > 0x%04x\n", dbuf->length, twb.flashsize);
						dbuf_free(dbuf);
						break;
					}

				} else if (op->memtype == OP_TYPE_EEPROM) {
					twb.progress_msg = "writing eeprom";

					if (dbuf->length > twb.eepromsize) {
						fprintf(stderr, "invalid eeprom size: 0x%04x > 0x%04x\n", dbuf->length, twb.eepromsize);
						dbuf_free(dbuf);
						break;
					}
				}

				result = twb_write(&twb, dbuf, op->memtype);
				if (result != 0x00) {
					fprintf(stderr, "failed to write to device\n");
					dbuf_free(dbuf);
					break;
				}

				if (verify) {
					if (op->memtype == OP_TYPE_FLASH) {
						twb.progress_msg = "verifing flash";
					} else if (op->memtype == OP_TYPE_EEPROM) {
						twb.progress_msg = "verifing eeprom";
					}

					result = twb_verify(&twb, dbuf, op->memtype);
					if (result != 0) {
						fprintf(stderr, "failed to verify\n");
						dbuf_free(dbuf);
						break;
					}
				}

				dbuf_free(dbuf);
			}
			abort = 0;
		}
	}

	struct operation *op, *tmp;
	list_for_each_entry_safe(op, tmp, &operation_list, list) {
		free(op->filename);
		free(op);
	}

	twb_close(&twb);

	return abort;
}
