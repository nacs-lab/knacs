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
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <asm/uaccess.h>

typedef struct {
    struct list_head node;
    unsigned long len;
    int num_pages;
    int finished:1;
    struct dma_chan *dma_chan;
    dma_cookie_t cookie;
    enum dma_data_direction dir;
    knacs_dma_page **pages;
    dma_addr_t *dma_addrs;
    struct scatterlist *sgs;
} knacs_dma_packet;

static DEFINE_SPINLOCK(knacs_dma_stream_lock);
static LIST_HEAD(knacs_dma_stream_to_write);
static LIST_HEAD(knacs_dma_stream_written_wait);

static LIST_HEAD(knacs_dma_stream_to_read);
static LIST_HEAD(knacs_dma_stream_read_wait);

static struct dma_chan *knacs_dma_stream_tx = NULL;
static struct dma_chan *knacs_dma_stream_rx = NULL;
static struct task_struct *knacs_slave_thread = NULL;
static DECLARE_COMPLETION(knacs_dma_slave_cmp);

static void
knacs_dma_stream_notify_slave(void)
{
    complete(&knacs_dma_slave_cmp);
}

static void
knacs_dma_packet_set_sg(knacs_dma_packet *packet, int i,
                        struct dma_device *dma_dev,
                        int len, enum dma_data_direction dir)
{
    dma_addr_t addr = dma_map_single(dma_dev->dev,
                                     packet->pages[i]->virt_addr, len, dir);
    packet->dma_addrs[i] = addr;
    sg_dma_address(&packet->sgs[i]) = addr;
    sg_dma_len(&packet->sgs[i]) = len;
}

static void
knacs_dma_packet_unmap(knacs_dma_packet *packet)
{
    if (!packet->dma_chan)
        return;
    struct dma_device *dma_dev = packet->dma_chan->device;
    int num_pages = packet->num_pages;
    for (int i = 0;i < num_pages;i++) {
        dma_unmap_single(dma_dev->dev, packet->dma_addrs[i],
                         PAGE_SIZE, packet->dir);
    }
    int len = packet->len - ((num_pages - 1) << PAGE_SHIFT);
    dma_unmap_single(dma_dev->dev, packet->dma_addrs[num_pages - 1],
                     len, packet->dir);
    packet->dma_chan = NULL;
}

static void
knacs_dma_packet_map(knacs_dma_packet *packet, struct dma_chan *chan,
                     enum dma_data_direction dir)
{
    struct dma_device *dma_dev = chan->device;
    if (unlikely(packet->dma_chan)) {
        if (packet->dma_chan == chan && packet->dir == dir)
            return;
        knacs_dma_packet_unmap(packet);
    }
    int num_pages = packet->num_pages;
    sg_init_table(packet->sgs, num_pages);
    for (int i = 0;i < num_pages - 1;i++) {
        knacs_dma_packet_set_sg(packet, i, dma_dev, PAGE_SIZE, dir);
    }
    int len = packet->len - ((num_pages - 1) << PAGE_SHIFT);
    knacs_dma_packet_set_sg(packet, num_pages - 1, dma_dev, len, dir);
    packet->dir = dir;
    packet->dma_chan = chan;
}

static knacs_dma_packet*
knacs_dma_packet_new_empty(unsigned long len)
{
    int num_pages = (len + PAGE_SIZE - 1) >> PAGE_SHIFT;
    size_t total_size = (sizeof(knacs_dma_packet) +
                         num_pages * (sizeof(knacs_dma_page*) +
                                      sizeof(dma_addr_t) +
                                      sizeof(struct scatterlist)));
    knacs_dma_packet *packet = kzalloc(total_size, GFP_KERNEL);
    if (!packet)
        return ERR_PTR(-ENOMEM);
    char *endof_struct = ((char*)packet) + sizeof(knacs_dma_packet);
    packet->len = len;
    packet->num_pages = num_pages;
    packet->pages = (knacs_dma_page**)endof_struct;
    packet->dma_addrs = (dma_addr_t*)(endof_struct +
                                      sizeof(knacs_dma_page*) * num_pages);
    packet->sgs = (struct scatterlist*)(endof_struct +
                                        (sizeof(knacs_dma_page*) +
                                         sizeof(dma_addr_t)) * num_pages);
    return packet;
}

static knacs_dma_packet*
knacs_dma_packet_new_from_area(knacs_dma_area *area, unsigned long len)
{
    knacs_dma_packet *packet = knacs_dma_packet_new_empty(len);
    if (IS_ERR(packet))
        return packet;
    int num_page_area;
    knacs_dma_page **pages =
        knacs_dma_area_get_all_pages(area, &num_page_area);
    for (int i = 0;i < packet->num_pages;i++) {
        packet->pages[i] = knacs_dma_page_ref(pages[i]);
    }
    return packet;
}

static knacs_dma_packet*
knacs_dma_packet_new(unsigned long len)
{
    knacs_dma_packet *packet = knacs_dma_packet_new_empty(len);
    if (IS_ERR(packet))
        return packet;
    for (int i = 0;i < packet->num_pages;i++) {
        packet->pages[i] = knacs_dma_page_new(1);
    }
    return packet;
}

static void
knacs_dma_packet_free(knacs_dma_packet *packet)
{
    knacs_dma_packet_unmap(packet);
    for (int i = 0;i < packet->num_pages;i++) {
        knacs_dma_page_unref(packet->pages[i]);
    }
    kfree(packet);
}

static void
knacs_dma_stream_callback(void *_packet)
{
    knacs_dma_packet *packet = _packet;
    unsigned long irqflags;
    spin_lock_irqsave(&knacs_dma_stream_lock, irqflags);
    packet->finished = 1;
    spin_unlock_irqrestore(&knacs_dma_stream_lock, irqflags);
    knacs_dma_stream_notify_slave();
}

static int
knacs_dma_stream_queue(knacs_dma_packet *packet, struct list_head *queue)
{
    struct dma_chan *chan = packet->dma_chan;
    if (unlikely(!chan)) {
        pr_alert("Cannot transfer unmaped packet.\n");
        return -EFAULT;
    }
    struct dma_device *dma_dev = chan->device;
    struct dma_async_tx_descriptor *desc =
        dma_dev->device_prep_slave_sg(chan, packet->sgs, packet->num_pages,
                                      packet->dir,
                                      DMA_CTRL_ACK | DMA_PREP_INTERRUPT, NULL);
    if (!desc) {
        pr_alert("Out of Memory.\n");
        return -ENOMEM;
    }
    unsigned long irqflags;

    spin_lock_irqsave(&knacs_dma_stream_lock, irqflags);
    list_del(&packet->node);
    list_add_tail(&packet->node, queue);
    spin_unlock_irqrestore(&knacs_dma_stream_lock, irqflags);

    desc->callback = knacs_dma_stream_callback;
    desc->callback_param = packet;
    packet->cookie = desc->tx_submit(desc);
    int err = dma_submit_error(packet->cookie);
    if (err) {
        pr_alert("DMA Submit error %d.\n", err);
        return err;
    }
    dma_async_issue_pending(chan);
    return 0;
}

static int
knacs_dma_stream_slave(void *data)
{
    while (!kthread_should_stop()) {
        wait_for_completion(&knacs_dma_slave_cmp);
        LIST_HEAD(packet_list);

        unsigned long irqflags;

        /* Writing */

        // For to_write list, transfer it into our local list first
        spin_lock_irqsave(&knacs_dma_stream_lock, irqflags);
        list_splice_tail_init(&knacs_dma_stream_to_write, &packet_list);
        spin_unlock_irqrestore(&knacs_dma_stream_lock, irqflags);

        knacs_dma_packet *packet;
        knacs_dma_packet *packet_next;
        // Now send every packet in our local list, queue them into the written
        // wait list at the same time.

        list_for_each_entry_safe(packet, packet_next, &packet_list, node) {
            if (knacs_dma_stream_queue(packet,
                                       &knacs_dma_stream_written_wait)) {
                pr_alert("Error sending packet\n");
                knacs_dma_packet_free(packet);
            } else {
                pr_info("Packet sent %p\n", packet);
            }
        }

        INIT_LIST_HEAD(&packet_list);

        // For each packet in the written_wait list, transfer the ones that
        // are done into our local list.
        spin_lock_irqsave(&knacs_dma_stream_lock, irqflags);
        list_for_each_entry_safe(packet, packet_next,
                                 &knacs_dma_stream_written_wait, node) {
            if (packet->finished) {
                list_del(&packet->node);
                list_add_tail(&packet->node, &packet_list);
            }
        }
        spin_unlock_irqrestore(&knacs_dma_stream_lock, irqflags);

        // Free the packets in the local list without holding the lock
        list_for_each_entry_safe(packet, packet_next, &packet_list, node) {
            pr_info("Packet sent done %p\n", packet);
            knacs_dma_packet_free(packet);
        }

        /* Reading */

        INIT_LIST_HEAD(&packet_list);
        int to_read_left = 0;

        // For each packet in the to_read list, move the finished one into
        // our local list, count empty packets at the same time.
        spin_lock_irqsave(&knacs_dma_stream_lock, irqflags);
        list_for_each_entry_safe(packet, packet_next,
                                 &knacs_dma_stream_to_read, node) {
            if (packet->finished) {
                list_del(&packet->node);
                list_add_tail(&packet->node, &packet_list);
            } else {
                to_read_left++;
            }
        }
        spin_unlock_irqrestore(&knacs_dma_stream_lock, irqflags);

        // Unmap the packets that are filled
        list_for_each_entry(packet, &packet_list, node) {
            pr_info("Packet filled %p\n", packet);
            knacs_dma_packet_unmap(packet);
        }

        // Queue the filled packets in the read_wait list
        spin_lock_irqsave(&knacs_dma_stream_lock, irqflags);
        list_for_each_entry_safe(packet, packet_next, &packet_list, node) {
            list_add_tail(&packet->node, &knacs_dma_stream_read_wait);
        }
        spin_unlock_irqrestore(&knacs_dma_stream_lock, irqflags);

        // If there's not enough (8) packets queued for reading, fill the gap.
        for (int i = 0;i < 8 - to_read_left;i++) {
            pr_info("Allocating packet %d\n", i);
            packet = knacs_dma_packet_new(PAGE_SIZE * 8);
            if (IS_ERR(packet)) {
                pr_alert("Could not allocate read buffer\n");
                continue;
            }
            knacs_dma_packet_map(packet, knacs_dma_stream_rx, DMA_DEV_TO_MEM);
            INIT_LIST_HEAD(&packet->node);
            if (knacs_dma_stream_queue(packet, &knacs_dma_stream_to_read)) {
                pr_alert("Error queueing read buffer\n");
                knacs_dma_packet_free(packet);
            } else {
                pr_info("Packet queued for read %p\n", packet);
            }
        }

        INIT_LIST_HEAD(&packet_list);
        spin_lock_irqsave(&knacs_dma_stream_lock, irqflags);
        list_splice_tail_init(&knacs_dma_stream_read_wait, &packet_list);
        spin_unlock_irqrestore(&knacs_dma_stream_lock, irqflags);

        // Free the read_wait packages to avoid oom when testing since
        // we don't have a way to get these buffers yet
        list_for_each_entry_safe(packet, packet_next, &packet_list, node) {
            pr_info("Packet sent done %p\n", packet);
            knacs_dma_packet_free(packet);
        }

        /* status = dma_async_is_tx_complete(tx_chan, tx_cookie, */
        /*                                   NULL, NULL); */
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
        if (!buff.len || vma->vm_start != buff_addr ||
            buff.len % 4 != 0)
            return -EINVAL;
        knacs_dma_area *area = vma->vm_private_data;
        knacs_dma_area_ref(area);

        vm_munmap(vma->vm_start, vma->vm_end - vma->vm_start);
        int num_pages;
        knacs_dma_page **pages =
            knacs_dma_area_get_all_pages(area, &num_pages);
        if (num_pages * PAGE_SIZE < buff.len) {
            knacs_dma_area_unref(area);
            return -EFAULT;
        }
        for (int i = 0;i < num_pages;i++) {
            if (i * PAGE_SIZE >= buff.len)
                break;
            if (!pages[i]) {
                knacs_dma_area_unref(area);
                return -EFAULT;
            }
        }
        knacs_dma_packet *packet =
            knacs_dma_packet_new_from_area(area, buff.len);
        knacs_dma_area_unref(area);
        if (IS_ERR(packet))
            return PTR_ERR(packet);
        knacs_dma_packet_map(packet, knacs_dma_stream_tx, DMA_MEM_TO_DEV);
        unsigned long irqflags;
        spin_lock_irqsave(&knacs_dma_stream_lock, irqflags);
        list_add_tail(&packet->node, &knacs_dma_stream_to_write);
        spin_unlock_irqrestore(&knacs_dma_stream_lock, irqflags);
        knacs_dma_stream_notify_slave();
        break;
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
    struct xilinx_dma_config config;

    knacs_dma_stream_tx = dma_request_slave_channel(&pdev->dev, "axidma0");
    if (IS_ERR(knacs_dma_stream_tx)) {
        err = PTR_ERR(knacs_dma_stream_tx);
        pr_alert("Requesting Tx channel failed\n");
        goto clear_tx;
    }
    /* Only one interrupt */
    config.coalesc = 1;
    config.delay = 0;
    xilinx_dma_channel_set_config(knacs_dma_stream_tx, &config);

    knacs_dma_stream_rx = dma_request_slave_channel(&pdev->dev, "axidma1");
    if (IS_ERR(knacs_dma_stream_rx)) {
        err = PTR_ERR(knacs_dma_stream_rx);
        pr_alert("Requesting Rx channel failed\n");
        goto free_tx;
    }
    /* Only one interrupt */
    config.coalesc = 1;
    config.delay = 0;
    xilinx_dma_channel_set_config(knacs_dma_stream_rx, &config);

    knacs_slave_thread = kthread_run(knacs_dma_stream_slave, NULL,
                                     "knacs-%s-%s",
                                     dma_chan_name(knacs_dma_stream_tx),
                                     dma_chan_name(knacs_dma_stream_rx));
    if (IS_ERR(knacs_slave_thread)) {
        err = PTR_ERR(knacs_slave_thread);
        pr_alert("Failed to create thread\n");
        goto free_thread;
    }

    knacs_dma_stream_notify_slave();
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
        knacs_dma_stream_notify_slave();
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
