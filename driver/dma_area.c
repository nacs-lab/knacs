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

#include "dma_area.h"
#include "dma_pages.h"

#include <linux/slab.h>
#include <linux/semaphore.h>

struct knacs_dma_area {
    atomic_t refcnt;
    struct semaphore mutex; // for changing ::pages
    int nalloc;
    int num_page;
    knacs_dma_page **pages;
};

knacs_dma_area*
knacs_dma_area_new(void)
{
    knacs_dma_area *area = kzalloc(sizeof(knacs_dma_area), GFP_KERNEL);
    if (!area)
        return ERR_PTR(-ENOMEM);
    atomic_set(&area->refcnt, 1);
    sema_init(&area->mutex, 1);
    return area;
}

knacs_dma_area*
knacs_dma_area_ref(knacs_dma_area *area)
{
    atomic_inc(&area->refcnt);
    return area;
}

void
knacs_dma_area_unref(knacs_dma_area *area)
{
    if (!atomic_dec_and_test(&area->refcnt))
        return;
    if (area->pages) {
        for (int i = 0;i < area->num_page;i++) {
            knacs_dma_page *page = area->pages[i];
            if (page) {
                knacs_dma_page_unref(page);
            }
        }
    }
    kfree(area);
}

static int
knacs_dma_page_ensure_page_slot(knacs_dma_area *area, int idx)
{
    if (unlikely(idx < 0))
        return -EINVAL;
    if (idx < area->nalloc)
        return 0;
    int nalloc = 1 << fls(idx);
    knacs_dma_page **new_pages = kzalloc(sizeof(knacs_dma_page*) * nalloc,
                                         GFP_KERNEL);
    if (unlikely(!new_pages))
        return -ENOMEM;
    if (area->pages) {
        memcpy(new_pages, area->pages,
               area->num_page * sizeof(knacs_dma_page*));
        kfree(area->pages);
    }
    area->pages = new_pages;
    area->nalloc = nalloc;
    return 0;
}

static knacs_dma_page*
_knacs_dma_area_get_page(knacs_dma_area *area, int idx, int zero_init)
{
    int err = knacs_dma_page_ensure_page_slot(area, idx);
    if (err) {
        return ERR_PTR(err);
    }
    if (area->pages[idx]) {
        return area->pages[idx];
    }
    knacs_dma_page *page = knacs_dma_page_new();
    area->pages[idx] = page;
    memset(page->virt_addr, 0, PAGE_SIZE);
    return page;
}

knacs_dma_page*
knacs_dma_area_get_page(knacs_dma_area *area, int idx, int zero_init)
{
    if (down_interruptible(&area->mutex))
        return ERR_PTR(-ERESTARTSYS);
    knacs_dma_page *page = _knacs_dma_area_get_page(area, idx, zero_init);
    up(&area->mutex);
    return page;
}

void
knacs_dma_area_lock(knacs_dma_area *area)
{
    down(&area->mutex);
}

void
knacs_dma_area_unlock(knacs_dma_area *area)
{
    up(&area->mutex);
}

knacs_dma_page**
knacs_dma_area_get_all_pages(knacs_dma_area *area, int *len)
{
    *len = area->num_page;
    return area->pages;
}
