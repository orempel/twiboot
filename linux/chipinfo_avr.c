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

#include <stdint.h>

#include "chipinfo_avr.h"

#define ARRAY_SIZE(x) (sizeof(x) / sizeof(*x))

struct chipinfo {
    uint8_t sig[3];
    const char name[16];
};

static struct chipinfo chips[] = {
    { { 0x1E, 0x93, 0x07 }, "AVR Mega 8" },
    { { 0x1E, 0x93, 0x0A }, "AVR Mega 88" },
    { { 0x1E, 0x94, 0x06 }, "AVR Mega 168" },
    { { 0x1E, 0x95, 0x02 }, "AVR Mega 32" },
};

const char * chipinfo_get_avr_name(const uint8_t *sig)
{
    int i;
    for (i = 0; i < ARRAY_SIZE(chips); i++) {
        struct chipinfo *chip = &chips[i];
        if (chip->sig[0] == sig[0] && chip->sig[1] == sig[1] && chip->sig[2] == sig[2])
            return chip->name;
    }

    return "unknown";
}
