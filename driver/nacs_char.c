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
static int nacs_dev_open(struct inode*, struct file*);
static int nacs_dev_release(struct inode*, struct file*);

/* static ssize_t nacs_dev_read(struct file*, char*, size_t, loff_t*); */
/* static ssize_t nacs_dev_write(struct file*, const char*, size_t, loff_t*); */

static long nacs_dev_ioctl(struct file *file, unsigned int cmd,
                           unsigned long arg);

static struct file_operations fops = {
    .owner = THIS_MODULE,
    .open = nacs_dev_open,
    /* .read = nacs_dev_read, */
    /* .write = nacs_dev_write, */
    .release = nacs_dev_release,
    .unlocked_ioctl = nacs_dev_ioctl,
};

static int majorNumber;
static struct class *nacsClass = NULL;
static struct device *knacsDevice = NULL;

static int __init knacs_init(void)
{
    int err = 0;
    // Try to dynamically allocate a major number for the device --
    // more difficult but worth it
    majorNumber = register_chrdev(0, DEVICE_NAME, &fops);
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
    pr_info("Device class created correctly\n");
    return 0;

dev_create_fail:
    class_destroy(nacsClass);
class_create_fail:
    unregister_chrdev(majorNumber, DEVICE_NAME);
reg_dev_fail:
    return err;
}

static void __exit knacs_exit(void)
{
    device_destroy(nacsClass, MKDEV(majorNumber, 0)); // remove the device
    class_unregister(nacsClass); // unregister the device class
    class_destroy(nacsClass); // remove the device class
    unregister_chrdev(majorNumber, DEVICE_NAME); // unregister the major number
    pr_debug("Goodbye.\n");
}

static int
nacs_dev_open(struct inode *inodep, struct file *filep)
{
    pr_debug("open()\n");
    return 0;
}

/* static ssize_t */
/* nacs_dev_read(struct file *filep, char *buffer, size_t len, loff_t *offset) */
/* { */
/* } */

/* static ssize_t */
/* nacs_dev_write(struct file *filep, const char *buffer, size_t len, */
/*                loff_t *offset) */
/* { */
/* } */

static int
nacs_dev_release(struct inode *inodep, struct file *filep)
{
    pr_debug("close()\n");
    return 0;
}

static long
nacs_dev_ioctl(struct file *file, unsigned int cmd, unsigned long _arg)
{
    long ret = 0;

    switch (cmd) {
    case KNACS_GET_VERSION: {
        const int major_ver = KNACS_MAJOR_VER;
        const int minor_ver = KNACS_MINOR_VER;
        knacs_version_t *arg = (knacs_version_t*)_arg;
        if (copy_to_user(&arg->major, &major_ver, sizeof(int)) ||
            copy_to_user(&arg->minor, &minor_ver, sizeof(int)))
            return -EFAULT;
    }
    default:
        break;
    }

    return ret;
}

module_init(knacs_init);
module_exit(knacs_exit);
