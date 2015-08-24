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

#define DEVICE_NAME "knacs"
#define CLASS_NAME "nacs"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Yichao Yu");
MODULE_DESCRIPTION("Linux driver for NaCs control system");
MODULE_VERSION("0.1");

static int majorNumber;
static char message[256] = {0};
static short size_of_message;
static int numberOpens = 0;
static struct class *nacsClass = NULL;
static struct device *knacsDevice = NULL;

// The prototype functions for the character driver -- must come before the
// struct definition
static int dev_open(struct inode*, struct file*);
static int dev_release(struct inode*, struct file*);
static ssize_t dev_read(struct file*, char*, size_t, loff_t*);
static ssize_t dev_write(struct file*, const char*, size_t, loff_t*);

static struct file_operations fops = {
    .open = dev_open,
    .read = dev_read,
    .write = dev_write,
    .release = dev_release,
};

static int __init knacs_init(void)
{
    // Try to dynamically allocate a major number for the device --
    // more difficult but worth it
    majorNumber = register_chrdev(0, DEVICE_NAME, &fops);
    if (majorNumber < 0) {
        pr_alert("Failed to register a major number\n");
        return majorNumber;
    }
    pr_info("Registered correctly with major number %d\n", majorNumber);

    // Register the device class
    nacsClass = class_create(THIS_MODULE, CLASS_NAME);
    if (IS_ERR(nacsClass)) {
        // Check for error and clean up if there is
        unregister_chrdev(majorNumber, DEVICE_NAME);
        pr_alert("Failed to register device class\n");
        // Correct way to return an error on a pointer
        return PTR_ERR(nacsClass);
    }
    pr_info("Device class registered correctly\n");

    // Register the device driver
    knacsDevice = device_create(nacsClass, NULL, MKDEV(majorNumber, 0), NULL,
                                DEVICE_NAME);
    if (IS_ERR(knacsDevice)) {
        // Clean up if there is an error
        class_destroy(nacsClass);
        unregister_chrdev(majorNumber, DEVICE_NAME);
        pr_alert("Failed to create the device\n");
        return PTR_ERR(knacsDevice);
    }
    pr_info("Device class created correctly\n");
    return 0;
}

static void __exit knacs_exit(void)
{
    device_destroy(nacsClass, MKDEV(majorNumber, 0)); // remove the device
    class_unregister(nacsClass); // unregister the device class
    class_destroy(nacsClass); // remove the device class
    unregister_chrdev(majorNumber, DEVICE_NAME); // unregister the major number
    pr_info("Goodbye from the LKM!\n");
}

static int dev_open(struct inode *inodep, struct file *filep)
{
    numberOpens++;
    pr_info("Device has been opened %d time(s)\n", numberOpens);
    return 0;
}

static ssize_t dev_read(struct file *filep, char *buffer, size_t len,
                        loff_t *offset)
{
    int error_count = 0;
    // copy_to_user has the format ( * to, *from, size) and returns 0 on success
    error_count = copy_to_user(buffer, message, size_of_message);

    if (error_count == 0) {
        pr_info("Sent %d characters to the user\n", size_of_message);
        size_of_message = 0;
        return 0;
    }
    else {
        pr_info("Failed to send %d characters to the user\n", error_count);
        return -EFAULT;
    }
}

static ssize_t dev_write(struct file *filep, const char *buffer, size_t len,
                         loff_t *offset)
{
    // appending received string with its length
    sprintf(message, "%s(%d letters)", buffer, len);
    // store the length of the stored message
    size_of_message = strlen(message);
    pr_info("Received %d characters from the user\n", len);
    return len;
}

static int dev_release(struct inode *inodep, struct file *filep)
{
    pr_info("Device successfully closed\n");
    return 0;
}

module_init(knacs_init);
module_exit(knacs_exit);
