/*************************************************************************
 *   Copyright (c) 2015 - 2015 Yichao Yu <yyc1992@gmail.com>             *
 *                                                                       *
 *   This program is free software; you can redistribute it and/or       *
 *   modify it under the terms of the GNU General Public License         *
 *   as published by the Free Software Foundation; either version 2      *
 *   of the License, or (at your option) any later version.              *
 *                                                                       *
 *   This program is distributed in the hope that it will be useful,     *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of      *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the       *
 *   GNU General Public License for more details.                        *
 *                                                                       *
 *   You should have received a copy of the GNU General Public License   *
 *   along with this program; if not, write to the Free Software         *
 *   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA       *
 *   02110-1301, USA.                                                    *
 *************************************************************************/

#ifndef __KNACS_H__
#define __KNACS_H__

#include <asm/ioctl.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int major;
    int minor;
} knacs_version_t;

typedef struct {
    unsigned long addr;
    unsigned long size;
    int l1_only: 1;
} knacs_dma_buff_t;

enum {
    KNACS_GET_VERSION = _IOW('y', 0, knacs_version_t),
    KNACS_GET_BUFF_PHY_ADDR = _IOWR('y', 1, unsigned long),
    KNACS_CLEAN_CACHE = _IOR('y', 2, knacs_dma_buff_t),
};

#ifdef __cplusplus
}
#endif

#endif
