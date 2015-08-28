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

#ifndef __KNACS_DMA_PAGES_H__
#define __KNACS_DMA_PAGES_H__

#include "obj_pool.h"

#include <linux/dmaengine.h>

typedef struct {
    struct list_head node;
    void *virt_addr;
    dma_addr_t dma_addr;
    atomic_t refcnt;
} knacs_dma_page;

int knacs_dma_pages_init(void);
void knacs_dma_pages_exit(void);
knacs_dma_page *knacs_dma_page_new(void);
knacs_dma_page *knacs_dma_page_ref(knacs_dma_page*);
void knacs_dma_page_unref(knacs_dma_page*);

#endif
