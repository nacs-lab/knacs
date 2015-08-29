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

#ifndef __KNACS_DMA_AREA_H__
#define __KNACS_DMA_AREA_H__

#include <linux/types.h>

struct knacs_dma_page;
typedef struct knacs_dma_area knacs_dma_area;

knacs_dma_area *knacs_dma_area_new(void);
knacs_dma_area *knacs_dma_area_ref(knacs_dma_area*);
void knacs_dma_area_unref(knacs_dma_area*);

void knacs_dma_area_lock(knacs_dma_area *area);
void knacs_dma_area_unlock(knacs_dma_area *area);

struct knacs_dma_page *knacs_dma_area_get_page(knacs_dma_area*, int,
                                               int zero_init);
struct knacs_dma_page **knacs_dma_area_get_all_pages(knacs_dma_area*, int*);

#endif
