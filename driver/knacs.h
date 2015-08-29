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

#ifdef __cplusplus
extern "C" {
#endif

enum {
    KNACS_GET_VERSION,
    _KNACS_IOCTL_GENERAL_MAX = (1 << 16),

    _KNACS_IOCTL_PULSE_CTRL_MAX = (1 << 16) * 2,
    KNACS_SEND_DMA_BUFFER,

    _KNACS_IOCTL_DMA_STREAM_MAX = (1 << 16) * 3,
};

typedef struct {
    int major;
    int minor;
} knacs_version_t;

typedef struct {
    unsigned long len;
    void *buff;
} knacs_dma_buffer_t;

#ifdef __cplusplus
}
#endif

#endif
