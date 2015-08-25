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

#include <linux/amba/xilinx_dma.h>
#include <linux/kthread.h>
#include <linux/delay.h>

static struct dma_chan *dma_stream_tx = NULL;
static struct dma_chan *dma_stream_rx = NULL;
static struct task_struct *slave_thread = NULL;

static int
knacs_dma_stream_slave(void *data)
{
    int counter = 0;
    while (!kthread_should_stop()) {
        if (counter++ % 300 == 10)
            pr_info("DMA slave: %d\n", counter);
        msleep(200);
    }
    return 0;
}

static int
knacs_dma_stream_probe(struct platform_device *pdev)
{
    if (dma_stream_tx || dma_stream_rx) {
        pr_alert("Only one pulse controller is allowed\n");
        return -EINVAL;
    }

    int err;
    dma_stream_tx = dma_request_slave_channel(&pdev->dev, "axidma0");
    if (IS_ERR(dma_stream_tx)) {
        err = PTR_ERR(dma_stream_tx);
        pr_alert("Requesting Tx channel failed\n");
        goto clear_tx;
    }

    dma_stream_rx = dma_request_slave_channel(&pdev->dev, "axidma1");
    if (IS_ERR(dma_stream_rx)) {
        err = PTR_ERR(dma_stream_rx);
        pr_alert("Requesting Rx channel failed\n");
        goto free_tx;
    }
    slave_thread = kthread_run(knacs_dma_stream_slave, NULL, "%s-%s",
                               dma_chan_name(dma_stream_tx),
                               dma_chan_name(dma_stream_rx));
    if (IS_ERR(slave_thread)) {
        err = PTR_ERR(slave_thread);
        pr_alert("Failed to create thread\n");
        goto free_thread;
    }

    /* srcbuf and dstbuf are allocated by the thread itself */
    get_task_struct(slave_thread);

    pr_info("dma streams created.\n");

    return 0;

free_thread:
    slave_thread = NULL;
    dma_release_channel(dma_stream_rx);
free_tx:
    dma_stream_rx = NULL;
    dma_release_channel(dma_stream_tx);
clear_tx:
    dma_stream_tx = NULL;
    return err;
}

static int
knacs_dma_stream_remove(struct platform_device *pdev)
{
    if (slave_thread) {
        kthread_stop(slave_thread);
        put_task_struct(slave_thread);
        slave_thread = NULL;
    }
    if (dma_stream_rx) {
        dma_release_channel(dma_stream_rx);
        dma_stream_rx = NULL;
    }
    if (dma_stream_tx) {
        dma_release_channel(dma_stream_tx);
        dma_stream_tx = NULL;
    }
    return 0;
}

static const struct of_device_id knacs_dma_stream_of_ids[] = {
    { .compatible = "nacs,pulser-ctrl-stream",},
    {}
};

struct platform_driver knacs_dma_stream_driver = {
    .driver = {
        .name = "knacs_dma_stream",
        .owner = THIS_MODULE,
        .of_match_table = knacs_dma_stream_of_ids,
    },
    .probe = knacs_dma_stream_probe,
    .remove = knacs_dma_stream_remove,
};
