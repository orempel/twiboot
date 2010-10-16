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

#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>

#include <linux/i2c.h>
#include <linux/i2c-dev.h>

#include "list.h"
#include "twiboot.h"

#define MIN(a, b) ((a) < (b) ? (a) : (b))

#define READ_BLOCK_SIZE		128	/* bytes in one flash/eeprom read request */
#define WRITE_BLOCK_SIZE	 16	/* bytes in one eeprom write request */

/* SLA+R */
#define CMD_WAIT		0x00
#define CMD_READ_VERSION	0x01
#define CMD_READ_MEMORY		0x02

/* SLA+W */
#define CMD_SWITCH_APPLICATION	CMD_READ_VERSION
#define CMD_WRITE_MEMORY	CMD_READ_MEMORY

/* CMD_SWITCH_APPLICATION parameter */
#define BOOTTYPE_BOOTLOADER	0x00				/* only in APP */
#define BOOTTYPE_APPLICATION	0x80

/* CMD_{READ|WRITE}_* parameter */
#define MEMTYPE_CHIPINFO	0x00
#define MEMTYPE_FLASH		0x01
#define MEMTYPE_EEPROM		0x02
#define MEMTYPE_PARAMETERS	0x03				/* only in APP */


static int twb_switch_application(struct twiboot *twb, uint8_t application)
{
	uint8_t cmd[] = { CMD_SWITCH_APPLICATION, application };

	return (write(twb->fd, cmd, sizeof(cmd)) != sizeof(cmd));
}

static int twb_read_version(struct twiboot *twb)
{
	uint8_t cmd[] = { CMD_READ_VERSION };

	if (write(twb->fd, cmd, sizeof(cmd)) != sizeof(cmd))
		return -1;

	memset(twb->version, 0, sizeof(twb->version));
	if (read(twb->fd, twb->version, sizeof(twb->version)) != sizeof(twb->version))
		return -1;

	int i;
	for (i = 0; i < sizeof(twb->version); i++)
		twb->version[i] &= ~0x80;

	return 0;
}

static int twb_read_memory(struct twiboot *twb, uint8_t *buffer, uint8_t size, uint8_t memtype, uint16_t address)
{
	uint8_t cmd[] = { CMD_READ_MEMORY, memtype, (address >> 8) & 0xFF, (address & 0xFF) };
	if (write(twb->fd, cmd, sizeof(cmd)) != sizeof(cmd))
		return -1;

	return (read(twb->fd, buffer, size) != size);
}

static int twb_write_memory(struct twiboot *twb, uint8_t *buffer, uint8_t size, uint8_t memtype, uint16_t address)
{
	int bufsize;
	if (memtype == MEMTYPE_FLASH) {
		if ((address & (twb->pagesize -1)) != 0x00) {
			fprintf(stderr, "twb_write_memory(): address 0x%04x not aligned to pagesize 0x%02x\n", address, twb->pagesize);
			return -1;
		}
		bufsize = 4 + twb->pagesize;

	} else {
		bufsize = 4 + size;
	}

	uint8_t *cmd = malloc(bufsize);
	if (cmd == NULL)
		return -1;

	cmd[0] = CMD_WRITE_MEMORY;
	cmd[1] = memtype;
	cmd[2] = (address >> 8) & 0xFF;
	cmd[3] = (address & 0xFF);
	memcpy(cmd +4, buffer, size);

	if (memtype == MEMTYPE_FLASH) {
		memset(cmd +4 +size, 0xFF, twb->pagesize - size);
	}

	int result = write(twb->fd, cmd, bufsize);
	free(cmd);

	return (result != bufsize);
}

static void twb_close_device(struct twiboot *twb)
{
	if (twb->connected)
		close(twb->fd);

	if (twb->device != NULL)
		free(twb->device);

	twb->device = NULL;
}

static int twb_open_device(struct twiboot *twb)
{
	twb->fd = open(twb->device, O_RDWR);
	if (twb->fd < 0) {
		fprintf(stderr, "failed to open '%s': %s\n", twb->device, strerror(errno));
		return -1;
	}

	unsigned long funcs;
	if (ioctl(twb->fd, I2C_FUNCS, &funcs)) {
		perror("ioctl(I2C_FUNCS)");
		close(twb->fd);
		return -1;
	}

	if (!(funcs & I2C_FUNC_I2C)) {
		fprintf(stderr, "I2C_FUNC_I2C not supported on '%s'!\n", twb->device);
		close(twb->fd);
		return -1;
	}

	if (ioctl(twb->fd, I2C_SLAVE, twb->address) < 0) {
		fprintf(stderr, "failed to select slave address '%d': %s\n", twb->address, strerror(errno));
		close(twb->fd);
		return -1;
	}

	twb->connected = 1;
	return 0;
}

int twb_close(struct twiboot *twb)
{
	if (twb->connected)
		twb_switch_application(twb, BOOTTYPE_APPLICATION);

	twb_close_device(twb);
	return 0;
}

int twb_open(struct twiboot *twb)
{
	if (twb_open_device(twb) != 0)
		return -1;

	if (twb_switch_application(twb, BOOTTYPE_BOOTLOADER)) {
		fprintf(stderr, "failed to switch to bootloader (invalid address?): %s\n", strerror(errno));
		twb_close(twb);
		return -1;
	}

	if (twb_read_version(twb)) {
		fprintf(stderr, "failed to get bootloader version: %s\n", strerror(errno));
		twb_close(twb);
		return -1;
	}

	uint8_t chipinfo[8];
	if (twb_read_memory(twb, chipinfo, sizeof(chipinfo), MEMTYPE_CHIPINFO, 0x0000)) {
		fprintf(stderr, "failed to get chipinfo: %s\n", strerror(errno));
		twb_close(twb);
		return -1;
	}

	memcpy(twb->signature, chipinfo, sizeof(twb->signature));
	twb->pagesize = chipinfo[3];
	twb->flashsize = (chipinfo[4] << 8) + chipinfo[5];
	twb->eepromsize = (chipinfo[6] << 8) + chipinfo[7];

	return 0;
}

int twb_read(struct twiboot *twb, struct databuf *dbuf, int memtype)
{
	int pos = 0;
	int size = (memtype == MEMTYPE_FLASH) ? twb->flashsize : twb->eepromsize;

	while (pos < size) {
		if (twb->progress_cb)
			twb->progress_cb(twb->progress_msg, pos, size);

		int len = MIN(READ_BLOCK_SIZE, size - pos);
		if (twb_read_memory(twb, dbuf->data + pos, len, memtype, pos)) {
			if (twb->progress_cb)
				twb->progress_cb(twb->progress_msg, -1, -1);

			return -1;
		}

		pos += len;
	}

	if (twb->progress_cb)
		twb->progress_cb(twb->progress_msg, pos, size);

	dbuf->length = pos;
	return 0;
}

int twb_write(struct twiboot *twb, struct databuf *dbuf, int memtype)
{
	int pos = 0;

	while (pos < dbuf->length) {
		if (twb->progress_cb)
			twb->progress_cb(twb->progress_msg, pos, dbuf->length);

		int len = (memtype == MEMTYPE_FLASH) ? twb->pagesize : WRITE_BLOCK_SIZE;

		len = MIN(len, dbuf->length - pos);
		if (twb_write_memory(twb, dbuf->data + pos, len, memtype, pos)) {
			if (twb->progress_cb)
				twb->progress_cb(twb->progress_msg, -1, -1);

			return -1;
		}

		pos += len;
	}

	if (twb->progress_cb)
		twb->progress_cb(twb->progress_msg, pos, dbuf->length);

	return 0;
}

int twb_verify(struct twiboot *twb, struct databuf *dbuf, int memtype)
{
	int pos = 0;
	int size = (memtype == MEMTYPE_FLASH) ? twb->flashsize : twb->eepromsize;
	uint8_t comp[READ_BLOCK_SIZE];

	while (pos < size) {
		if (twb->progress_cb)
			twb->progress_cb(twb->progress_msg, pos, size);

		int len = MIN(READ_BLOCK_SIZE, size - pos);
		if (twb_read_memory(twb, comp, len, memtype, pos)) {
			if (twb->progress_cb)
				twb->progress_cb(twb->progress_msg, -1, -1);

			return -1;
		}

		if (memcmp(comp, dbuf->data + pos, len) != 0x00) {
			if (twb->progress_cb)
				twb->progress_cb(twb->progress_msg, -1, -1);

			fprintf(stderr, "verify failed at page 0x%04x!!\n", pos);
			return -1;
		}

		pos += len;
	}

	if (twb->progress_cb)
		twb->progress_cb(twb->progress_msg, pos, size);

	dbuf->length = pos;
	return 0;
}
