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

#include "dma_stream.h"
#include "dma_pages.h"
#include "dma_area.h"
#include "knacs.h"

#include <linux/amba/xilinx_dma.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <asm/uaccess.h>

static struct dma_chan *knacs_dma_stream_tx = NULL;
static struct dma_chan *knacs_dma_stream_rx = NULL;
static struct task_struct *knacs_slave_thread = NULL;

static int
knacs_dma_stream_slave(void *data)
{
    while (!kthread_should_stop()) {
        msleep(200);
    }
    return 0;
}

static void
knacs_dma_stream_vm_open(struct vm_area_struct *vma)
{
    knacs_dma_area *area = vma->vm_private_data;
    knacs_dma_area_ref(area);
}

static void
knacs_dma_stream_vm_close(struct vm_area_struct *vma)
{
    knacs_dma_area *area = vma->vm_private_data;
    knacs_dma_area_unref(area);
}

static int
knacs_dma_stream_vm_fault(struct vm_area_struct *vma, struct vm_fault *vmf)
{
    knacs_dma_area *area = vma->vm_private_data;
    pgoff_t index = vmf->pgoff - 1;
    knacs_dma_page *page = knacs_dma_area_get_page(area, index, 1);
    if (IS_ERR(page))
        return VM_FAULT_OOM;

    /*
     * Get the page, inc the use count, and return it
     */
    struct page *struct_page = virt_to_page(page->virt_addr);
    if (!struct_page)
        return VM_FAULT_SIGBUS;
    get_page(struct_page);
    vmf->page = struct_page;
    return 0;
}

static const struct vm_operations_struct knacs_dma_stream_vm_ops = {
    .open = knacs_dma_stream_vm_open,
    .close = knacs_dma_stream_vm_close,
    .fault = knacs_dma_stream_vm_fault,
};

int
knacs_dma_stream_mmap(struct file *filp, struct vm_area_struct *vma)
{
    vma->vm_ops = &knacs_dma_stream_vm_ops;
    vma->vm_private_data = knacs_dma_area_new();
    return 0;
}

long
knacs_dma_stream_ioctl(struct file *filp, unsigned int cmd, unsigned long _arg)
{
    switch (cmd) {
    case KNACS_SEND_DMA_BUFFER: {
        knacs_dma_buffer_t buff = {0, 0};
        if (copy_from_user(&buff, (void*)_arg, sizeof(knacs_dma_buffer_t)))
            return -EFAULT;
        unsigned long buff_addr = (unsigned long)buff.buff;
        struct vm_area_struct *vma = find_vma(current->mm, buff_addr);
        if (!vma || vma->vm_file != filp || vma->vm_pgoff != 1 ||
            vma->vm_start + buff.len > vma->vm_end)
            return -EFAULT;
        vm_munmap(vma->vm_start, vma->vm_end - vma->vm_start);
    }
    default:
        return -EINVAL;
    }
    return 0;
}

static int
knacs_dma_stream_probe(struct platform_device *pdev)
{
    if (knacs_dma_stream_tx || knacs_dma_stream_rx) {
        pr_alert("Only one pulse controller is allowed\n");
        return -EINVAL;
    }

    int err;
    knacs_dma_stream_tx = dma_request_slave_channel(&pdev->dev, "axidma0");
    if (IS_ERR(knacs_dma_stream_tx)) {
        err = PTR_ERR(knacs_dma_stream_tx);
        pr_alert("Requesting Tx channel failed\n");
        goto clear_tx;
    }

    knacs_dma_stream_rx = dma_request_slave_channel(&pdev->dev, "axidma1");
    if (IS_ERR(knacs_dma_stream_rx)) {
        err = PTR_ERR(knacs_dma_stream_rx);
        pr_alert("Requesting Rx channel failed\n");
        goto free_tx;
    }
    knacs_slave_thread = kthread_run(knacs_dma_stream_slave, NULL,
                                     "knacs-%s-%s",
                                     dma_chan_name(knacs_dma_stream_tx),
                                     dma_chan_name(knacs_dma_stream_rx));
    if (IS_ERR(knacs_slave_thread)) {
        err = PTR_ERR(knacs_slave_thread);
        pr_alert("Failed to create thread\n");
        goto free_thread;
    }

    /* srcbuf and dstbuf are allocated by the thread itself */
    get_task_struct(knacs_slave_thread);

    pr_info("dma streams created.\n");

    return 0;

free_thread:
    knacs_slave_thread = NULL;
    dma_release_channel(knacs_dma_stream_rx);
free_tx:
    knacs_dma_stream_rx = NULL;
    dma_release_channel(knacs_dma_stream_tx);
clear_tx:
    knacs_dma_stream_tx = NULL;
    return err;
}

static int
knacs_dma_stream_remove(struct platform_device *pdev)
{
    if (knacs_slave_thread) {
        kthread_stop(knacs_slave_thread);
        put_task_struct(knacs_slave_thread);
        knacs_slave_thread = NULL;
    }
    if (knacs_dma_stream_rx) {
        dma_release_channel(knacs_dma_stream_rx);
        knacs_dma_stream_rx = NULL;
    }
    if (knacs_dma_stream_tx) {
        dma_release_channel(knacs_dma_stream_tx);
        knacs_dma_stream_tx = NULL;
    }
    return 0;
}

static const struct of_device_id knacs_dma_stream_of_ids[] = {
    { .compatible = "nacs,pulser-ctrl-stream",},
    {}
};

static struct platform_driver knacs_dma_stream_driver = {
    .driver = {
        .name = "knacs_dma_stream",
        .owner = THIS_MODULE,
        .of_match_table = knacs_dma_stream_of_ids,
    },
    .probe = knacs_dma_stream_probe,
    .remove = knacs_dma_stream_remove,
};

int __init
knacs_dma_stream_init(void)
{
    int err;
    if ((err = knacs_dma_pages_init()))
        goto err;

    if ((err = platform_driver_register(&knacs_dma_stream_driver))) {
        pr_alert("Failed to register dma stream driver\n");
        goto pages_exit;
    }
    return 0;

pages_exit:
    knacs_dma_pages_exit();
err:
    return err;
}

void
knacs_dma_stream_exit(void)
{
    platform_driver_unregister(&knacs_dma_stream_driver);
    knacs_dma_pages_exit();
}
