/*************************************************************************
 *   Copyright (c) 2021 - 2021 Yichao Yu <yyc1992@gmail.com>             *
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

#define pr_fmt(fmt) "KNaCs (dma-buff): " fmt

/**
 * Similar to the OCM manager, this allocates physically contiguous buffers from normal memory.
 */

#include "dma_buff.h"

#include "buff_alloc.h"
#include "knacs.h"

#include <linux/genalloc.h>
#include <linux/slab.h>

#define MAX_PAGE_ORDER 6
#define MAX_PAGE_COUNT (1 << MAX_PAGE_ORDER)

static struct gen_pool *dma_buff_pool = NULL;
static struct page *all_pages[MAX_PAGE_COUNT] = {};
static unsigned int all_orders[MAX_PAGE_COUNT] = {};

int __init knacs_dma_buff_init(void)
{
    // Logic copied from `arch/arm/mach-zynq/zynq_ocm.c`
    // Don't fail the module loading if this fail, we'll simply fail at allocation time instead.
    dma_buff_pool = gen_pool_create(PAGE_SHIFT, NUMA_NO_NODE);
    if (!dma_buff_pool)
        return 0;

    // Try to allocate 64 pages in total, with as large chuncks as possible.
    unsigned int order = MAX_PAGE_ORDER;
    unsigned int allocated = 0;
    unsigned int all_pages_idx = 0;

    while (allocated < MAX_PAGE_COUNT) {
        struct page *page = alloc_pages(GFP_KERNEL, order);
        if (!page) {
            if (order == 0)
                break;
            order--;
            continue;
        }
        unsigned int count = 1 << order;
        void *virt_addr = page_address(page);
        unsigned long phy_addr = page_to_pfn(page) << PAGE_SHIFT;
        int ret = gen_pool_add_virt(dma_buff_pool, (unsigned long)virt_addr,
                                    phy_addr, count << PAGE_SHIFT, -1);
        if (ret < 0) {
            pr_alert("Unable to add page to pool %d\n", ret);
            __free_pages(page, order);
            break;
        }
        pr_info("Allocated %u pages at %lx\n", count, phy_addr);
        all_pages[all_pages_idx] = page;
        all_orders[all_pages_idx] = order;
        all_pages_idx++;
        allocated += count;
    }

    if (allocated < MAX_PAGE_COUNT)
        pr_alert("Unable to allocate %u pages, %u allocated\n", MAX_PAGE_COUNT, allocated);

    return 0;
}

void __exit knacs_dma_buff_exit(void)
{
    if (gen_pool_avail(dma_buff_pool) < gen_pool_size(dma_buff_pool)) {
        pr_warn("Exit while buffer allocated.\n");
        return;
    }

    gen_pool_destroy(dma_buff_pool);
    dma_buff_pool = NULL;

    for (unsigned int i = 0; i < MAX_PAGE_COUNT; i++) {
        if (!all_pages[i])
            break;
        __free_pages(all_pages[i], all_orders[i]);
    }
    all_pages[0] = NULL;
}

int knacs_dma_buff_mmap(struct file *file, struct vm_area_struct *vma)
{
    return knacs_buff_alloc_mmap(dma_buff_pool, vma, "DMA Buff");
}
