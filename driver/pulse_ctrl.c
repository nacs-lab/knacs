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

#include "pulse_ctrl.h"

const static size_t ctrler_addr = 0x73000000;

int knacs_pulse_ctl_mmap(struct file *filp, struct vm_area_struct *vma)
{
    unsigned long requested_size = vma->vm_end - vma->vm_start;
    if (requested_size > PAGE_SIZE) {
        pr_alert("MMap size too large for pulse controller\n");
        return -EINVAL;
    }

    vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
    vma->vm_flags |= VM_IO;

    pr_debug("mmap pulse controller\n");

    return remap_pfn_range(vma, vma->vm_start, ctrler_addr >> PAGE_SHIFT,
                           requested_size, vma->vm_page_prot);
}

int __init knacs_pulse_ctl_init(void)
{
    return 0;
}

void __exit knacs_pulse_ctl_exit(void)
{
}
