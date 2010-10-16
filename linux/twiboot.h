#ifndef _TWIBOOT_H_
#define _TWIBOOT_H_

#include <stdint.h>

struct databuf {
	uint32_t size;		// allocation size
	uint32_t length;	// used size
	uint8_t data[0];
};

int dbuf_alloc(struct databuf **dbuf, uint32_t size);
void dbuf_free(struct databuf *dbuf);

int file_getsize(const char *filename, uint32_t *size);
int file_read(const char *filename, struct databuf *dbuf);
int file_write(const char *filename, struct databuf *dbuf);

struct twiboot {
	char *device;
	uint8_t	address;
	int fd;
	int connected;

	char version[16];
	uint8_t signature[3];
	uint8_t pagesize;
	uint16_t flashsize;
	uint16_t eepromsize;

	void (* progress_cb)(const char *msg, int pos, int max);
	char *progress_msg;
};

int twb_open(struct twiboot *twb);
int twb_close(struct twiboot *twb);

#define DATATYPE_FLASH		0x01
#define DATATYPE_EEPROM		0x02

int twb_read(struct twiboot *twb, struct databuf *dbuf, int memtype);
int twb_verify(struct twiboot *twb, struct databuf *dbuf, int memtype);
int twb_write(struct twiboot *twb, struct databuf *dbuf, int memtype);

#endif /* _TWIBOOT_H_ */
