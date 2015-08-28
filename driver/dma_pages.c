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

#include "dma_pages.h"

#include <linux/slab.h>

static knacs_objpool *knacs_dma_pages_pool = NULL;

static void*
knacs_dma_page_alloc(void *data)
{
    (void)data;
    void *addr = (void*)__get_free_page(GFP_KERNEL | __GFP_DMA);
    if (!addr)
        return NULL;
    knacs_dma_page *page = kmalloc(sizeof(knacs_dma_page),
                                          GFP_KERNEL);
    if (!page)
        return NULL;
    page->virt_addr = page;
    return page;
}

static void
knacs_dma_page_free(void *_page, void *data)
{
    (void)data;
    knacs_dma_page *page = _page;
    free_page((unsigned long)page->virt_addr);
    kfree(_page);
}

knacs_dma_page*
knacs_dma_page_new(void)
{
    knacs_dma_page *page = knacs_objpool_alloc(knacs_dma_pages_pool);
    if (IS_ERR(page))
        return page;
    page->dma_addr = 0;
    atomic_set(&page->refcnt, 1);
    return page;
}

knacs_dma_page*
knacs_dma_page_ref(knacs_dma_page *page)
{
    atomic_inc(&page->refcnt);
    return page;
}

void
knacs_dma_page_unref(knacs_dma_page *page)
{
    if (!atomic_dec_and_test(&page->refcnt))
        return;
    knacs_objpool_free(knacs_dma_pages_pool, (void*)page);
}

int __init
knacs_dma_pages_init(void)
{
    int err;

    knacs_dma_pages_pool =
        knacs_objpool_create(128, knacs_dma_page_alloc,
                             knacs_dma_page_free, NULL);
    if (IS_ERR(knacs_dma_pages_pool)) {
        pr_alert("Failed to allocate dma pages pool\n");
        err = PTR_ERR(knacs_dma_pages_pool);
        goto err;
    }
    return 0;
err:
    return err;
}

void
knacs_dma_pages_exit(void)
{
    knacs_objpool_destroy(knacs_dma_pages_pool);
}
