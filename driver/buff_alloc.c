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

#define pr_fmt(fmt) "KNaCs (buff-alloc): " fmt

#include "buff_alloc.h"

#include <linux/slab.h>

struct vm_buf {
    struct gen_pool *pool;
    void *virt_addr;
    size_t sz;
    refcount_t refcnt;
};

// Open and close implementation borrowed from `drivers/char/mspec.c`
static void buff_vm_open(struct vm_area_struct *vma)
{
    struct vm_buf *vm_buf = vma->vm_private_data;
    refcount_inc(&vm_buf->refcnt);
}

static void buff_vm_close(struct vm_area_struct *vma)
{
    struct vm_buf *vm_buf = vma->vm_private_data;
    if (!refcount_dec_and_test(&vm_buf->refcnt))
        return;

    gen_pool_free(vm_buf->pool, (unsigned long)vm_buf->virt_addr, vm_buf->sz);
    kfree(vm_buf);
}

static const struct vm_operations_struct buff_vm_ops = {
    .open = buff_vm_open,
    .close = buff_vm_close,
};

int knacs_buff_alloc_mmap(struct gen_pool *pool, struct vm_area_struct *vma, const char *name)
{
    if ((vma->vm_flags & (VM_SHARED | VM_MAYSHARE)) == 0)
        return -EINVAL;

    unsigned long sz = vma->vm_end - vma->vm_start;
    if (sz == 0)
        return -EINVAL;

    // Allocation logic modified from `arch/arm/mach-zynq/pm.c`
    dma_addr_t dma_addr = 0;
    void *virt_addr = gen_pool_dma_alloc_align(pool, sz, &dma_addr, PAGE_SIZE);
    if (!virt_addr) {
        pr_debug("Unable to allocate %s buffer\n", name);
        return -ENOMEM;
    }

    int ret = -ENOMEM;
    if (dma_addr == (dma_addr_t)-1) {
        pr_alert("Unable to find physical address of %s buffer\n", name);
        goto failed;
    }

    struct vm_buf *vm_buf = kzalloc(sizeof(struct vm_buf), GFP_KERNEL);
    if (!vm_buf) {
        pr_alert("kalloc failed for vm_buf\n");
        goto failed;
    }
    vm_buf->pool = pool;
    vm_buf->virt_addr = virt_addr;
    vm_buf->sz = sz;
    refcount_set(&vm_buf->refcnt, 1);

    // mapping implementation borrowed from `drivers/char/mem.c`
    vma->vm_private_data = vm_buf;
    vma->vm_ops = &buff_vm_ops;
    memset(virt_addr, 0, sz);

    pr_debug("Allocated %s buffer of size %lu @ 0x%lx\n",
             name, (unsigned long)sz, (unsigned long)dma_addr);

    return remap_pfn_range(vma, vma->vm_start, dma_addr >> PAGE_SHIFT,
                           sz, vma->vm_page_prot);

failed:
    gen_pool_free(pool, (unsigned long)virt_addr, sz);
    return ret;
}
