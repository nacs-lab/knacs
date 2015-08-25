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

static struct dma_chan *dma_stream_tx = NULL;
static struct dma_chan *dma_stream_rx = NULL;

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

    pr_info("dma streams created.\n");

    return 0;

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
    if (dma_stream_rx) {
        dma_stream_rx = NULL;
        dma_release_channel(dma_stream_rx);
    }
    if (dma_stream_tx) {
        dma_stream_tx = NULL;
        dma_release_channel(dma_stream_tx);
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
