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

// This code is based on the example by Derek Molloy at
// http://derekmolloy.ie/writing-a-linux-kernel-module-part-2-a-character-device/

#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/platform_device.h>

#include <linux/of_address.h>
#include <linux/of_dma.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>

#include <asm/uaccess.h>

#include "knacs.h"

#define DEVICE_NAME "knacs"
#define CLASS_NAME "nacs"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Yichao Yu");
MODULE_DESCRIPTION("Linux driver for NaCs control system");
MODULE_VERSION("0.1");

#define KNACS_MAJOR_VER 0
#define KNACS_MINOR_VER 1

// The prototype functions for the character driver -- must come before the
// struct definition
static int knacs_dev_open(struct inode*, struct file*);
static int knacs_dev_release(struct inode*, struct file*);

/* static ssize_t knacs_dev_read(struct file*, char*, size_t, loff_t*); */
/* static ssize_t knacs_dev_write(struct file*, const char*, size_t, loff_t*); */

static long knacs_dev_ioctl(struct file *file, unsigned int cmd,
                            unsigned long arg);
static int knacs_dev_mmap(struct file *filp, struct vm_area_struct *vma);

static struct file_operations knacs_fops = {
    .owner = THIS_MODULE,
    .open = knacs_dev_open,
    /* .read = knacs_dev_read, */
    /* .write = knacs_dev_write, */
    .release = knacs_dev_release,
    .mmap = knacs_dev_mmap,
    .unlocked_ioctl = knacs_dev_ioctl,
};

static int majorNumber;
static struct class *nacsClass = NULL;
static struct device *knacsDevice = NULL;

static int knacs_pulse_ctl_probe(struct platform_device*);
static int knacs_pulse_ctl_remove(struct platform_device*);

static const struct of_device_id knacs_pulse_ctl_of_ids[] = {
    { .compatible = "xlnx,pulse-controller-5.0",},
    {}
};

static struct platform_driver knacs_pulse_ctl_driver = {
    .driver = {
        .name = "knacs_pulse_controller",
        .owner = THIS_MODULE,
        .of_match_table = knacs_pulse_ctl_of_ids,
    },
    .probe = knacs_pulse_ctl_probe,
    .remove = knacs_pulse_ctl_remove,
};

static int __init knacs_init(void)
{
    int err = 0;
    // Try to dynamically allocate a major number for the device --
    // more difficult but worth it
    majorNumber = register_chrdev(0, DEVICE_NAME, &knacs_fops);
    if (majorNumber < 0) {
        pr_alert("Failed to register a major number\n");
        err = majorNumber;
        goto reg_dev_fail;
    }

    // Register the device class
    nacsClass = class_create(THIS_MODULE, CLASS_NAME);
    if (IS_ERR(nacsClass)) {
        pr_alert("Failed to register device class\n");
        // Correct way to return an error on a pointer
        err = PTR_ERR(nacsClass);
        goto class_create_fail;
    }

    // Register the device driver
    knacsDevice = device_create(nacsClass, NULL, MKDEV(majorNumber, 0), NULL,
                                DEVICE_NAME);
    if (IS_ERR(knacsDevice)) {
        pr_alert("Failed to create the device\n");
        err = PTR_ERR(knacsDevice);
        goto dev_create_fail;
    }

    if ((err = platform_driver_register(&knacs_pulse_ctl_driver))) {
        pr_alert("Failed to register pulse controller driver\n");
        goto pulse_ctl_reg_fail;
    }
    return 0;

pulse_ctl_reg_fail:
    device_destroy(nacsClass, MKDEV(majorNumber, 0)); // remove the device
dev_create_fail:
    class_destroy(nacsClass);
class_create_fail:
    unregister_chrdev(majorNumber, DEVICE_NAME);
reg_dev_fail:
    return err;
}

static void __exit knacs_exit(void)
{
    platform_driver_unregister(&knacs_pulse_ctl_driver);
    device_destroy(nacsClass, MKDEV(majorNumber, 0)); // remove the device
    class_unregister(nacsClass); // unregister the device class
    class_destroy(nacsClass); // remove the device class
    unregister_chrdev(majorNumber, DEVICE_NAME); // unregister the major number
    pr_debug("Goodbye.\n");
}

static struct resource *pulse_ctl_regs = NULL;
static int knacs_pulse_ctl_probe(struct platform_device *pdev)
{
    if (pulse_ctl_regs) {
        pr_alert("Only one pulse controller is allowed\n");
        return -EINVAL;
    }
    // Sanity check, should not be necessary
    if (!of_match_device(knacs_pulse_ctl_of_ids, &pdev->dev))
        return -EINVAL;
    pulse_ctl_regs = platform_get_resource(pdev, IORESOURCE_MEM, 0);
    if  (!pulse_ctl_regs ||
         !request_mem_region(pulse_ctl_regs->start,
                             resource_size(pulse_ctl_regs),
                             "knacs-pulse-controller")) {
        pr_alert("Failed to request pulse controller registers\n");
        return -EBUSY;
    }
    pr_info("pulse controller probe\n");
    pr_info("    res->start @0x%x\n", pulse_ctl_regs->start);

    return 0;
}

static int knacs_pulse_ctl_remove(struct platform_device *pdev)
{
    release_mem_region(pulse_ctl_regs->start, resource_size(pulse_ctl_regs));
    pulse_ctl_regs = NULL;
    return 0;
}

static int
knacs_dev_open(struct inode *inodep, struct file *filep)
{
    pr_debug("open()\n");
    return 0;
}

/* static ssize_t */
/* knacs_dev_read(struct file *filep, char *buffer, size_t len, loff_t *offset) */
/* { */
/* } */

/* static ssize_t */
/* knacs_dev_write(struct file *filep, const char *buffer, size_t len, */
/*                loff_t *offset) */
/* { */
/* } */

static int
knacs_dev_release(struct inode *inodep, struct file *filep)
{
    pr_debug("close()\n");
    return 0;
}

static long
knacs_dev_ioctl(struct file *file, unsigned int cmd, unsigned long _arg)
{
    switch (cmd) {
    case KNACS_GET_VERSION: {
        const int major_ver = KNACS_MAJOR_VER;
        const int minor_ver = KNACS_MINOR_VER;
        knacs_version_t *arg = (knacs_version_t*)_arg;
        if (copy_to_user(&arg->major, &major_ver, sizeof(int)) ||
            copy_to_user(&arg->minor, &minor_ver, sizeof(int))) {
            return -EFAULT;
        }
        break;
    }
    default:
        return -EINVAL;
    }

    return 0;
}

static int
knacs_dev_mmap_pulse_ctl(struct file *filp, struct vm_area_struct *vma)
{
    unsigned long requested_size = vma->vm_end - vma->vm_start;
    if (requested_size > resource_size(pulse_ctl_regs)) {
        pr_alert("MMap size too large for pulse controller\n");
        return -EINVAL;
    }

    vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
    vma->vm_flags |= VM_IO;

    return remap_pfn_range(vma, vma->vm_start,
                           pulse_ctl_regs->start >> PAGE_SHIFT,
                           requested_size, vma->vm_page_prot);
}

static int
knacs_dev_mmap(struct file *filp, struct vm_area_struct *vma)
{
    // The first page is the pulse controller registers
    if (vma->vm_pgoff == 0)
        return knacs_dev_mmap_pulse_ctl(filp, vma);
    pr_alert("Mapping unknown pages.\n");
    return -EINVAL;
}

module_init(knacs_init);
module_exit(knacs_exit);
