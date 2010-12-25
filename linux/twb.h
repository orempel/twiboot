#ifndef _TWB_H_
#define _TWB_H_

#include <stdint.h>

struct twiboot {
	char *device;
	uint8_t	address;
	int fd;
	int connected;

	char version[16];
	uint8_t signature[3];
	const char *chipname;

	uint8_t pagesize;
	uint16_t flashsize;
	uint16_t eepromsize;

	void (* progress_cb)(const char *msg, int pos, int max);
	char *progress_msg;
};

int twb_open(struct twiboot *twb);
int twb_close(struct twiboot *twb);

int twb_read(struct twiboot *twb, struct databuf *dbuf, int memtype);
int twb_verify(struct twiboot *twb, struct databuf *dbuf, int memtype);
int twb_write(struct twiboot *twb, struct databuf *dbuf, int memtype);

#endif /* _TWIBOOT_H_ */
