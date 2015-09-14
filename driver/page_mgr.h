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

#ifndef __KNACS_PAGE_MGR_H__
#define __KNACS_PAGE_MGR_H__

#include <linux/dmaengine.h>
#include <linux/rbtree.h>

struct knacs_dma_sg_desc;

typedef struct {
    struct rb_node node; // For chaining into a block
    struct knacs_dma_sg_desc *sg; // SG descriptor (may be NULL)
    u32 idx; // page index in the block
    void *virt_addr; // kernel virtual address
    dma_addr_t dma_addr;
} knacs_page;

int knacs_pages_init(void);
void knacs_pages_exit(void);

knacs_page *knacs_page_new(int zero_init);
void knacs_page_free(knacs_page*);

#endif
