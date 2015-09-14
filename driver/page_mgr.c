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

#define pr_fmt(fmt) "KNaCs: " fmt

#include "page_mgr.h"
#include "obj_pool.h"

#include <linux/slab.h>

static knacs_objpool *knacs_pages_pool = NULL;

static void*
knacs_page_alloc(void *data)
{
    (void)data;
    void *addr = (void*)__get_free_page(GFP_KERNEL | __GFP_DMA);
    if (!addr)
        return NULL;
    knacs_page *page = kmalloc(sizeof(knacs_page), GFP_KERNEL);
    if (!page) {
        free_page((unsigned long)addr);
        return NULL;
    }
    page->virt_addr = addr;
    return page;
}

static void
knacs_page_destroy(void *_page, void *data)
{
    (void)data;
    knacs_page *page = _page;
    free_page((unsigned long)page->virt_addr);
    kfree(_page);
}

knacs_page*
knacs_page_new(int zero_init)
{
    knacs_page *page = knacs_objpool_alloc(knacs_pages_pool);
    if (IS_ERR(page))
        return page;
    page->sg = NULL;
    page->dma_addr = 0;
    if (zero_init)
        memset(page->virt_addr, 0, PAGE_SIZE);
    return page;
}

void
knacs_page_free(knacs_page *page)
{
    knacs_objpool_free(knacs_pages_pool, (void*)page);
}

int __init
knacs_pages_init(void)
{
    int err;

    knacs_pages_pool =
        knacs_objpool_create(256, knacs_page_alloc, knacs_page_destroy, NULL);
    if (IS_ERR(knacs_pages_pool)) {
        pr_alert("Failed to allocate dma pages pool\n");
        err = PTR_ERR(knacs_pages_pool);
        goto err;
    }
    return 0;
err:
    return err;
}

void
knacs_pages_exit(void)
{
    knacs_objpool_destroy(knacs_pages_pool);
}
