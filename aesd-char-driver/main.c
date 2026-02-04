/**
 * @file aesdchar.c
 * @brief Functions and data related to the AESD char driver implementation
 *
 * Based on the implementation of the "scull" device driver, found in
 * Linux Device Drivers example code.
 *
 * @author Dan Walkes
 * @date 2019-10-22
 * @copyright Copyright (c) 2019
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/fs.h> // file_operations
#include <linux/slab.h>
#include <linux/uaccess.h>
#include "aesdchar.h"
#include "aesd_ioctl.h"

int aesd_major =   0; // use dynamic major
int aesd_minor =   0;

MODULE_AUTHOR("Mathalama");
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;

int aesd_open(struct inode *inode, struct file *filp)
{
    struct aesd_dev *dev;
    PDEBUG("open");
    dev = container_of(inode->i_cdev, struct aesd_dev, cdev);
    filp->private_data = dev;
    return 0;
}

int aesd_release(struct inode *inode, struct file *filp)
{
    PDEBUG("release");
    return 0;
}

ssize_t aesd_read(struct file *filp, char __user *buf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval = 0;
    struct aesd_dev *dev = filp->private_data;
    struct aesd_buffer_entry *entry;
    size_t entry_offset_byte = 0;
    size_t bytes_to_copy;

    PDEBUG("read %zu bytes with offset %lld",count,*f_pos);

    if (mutex_lock_interruptible(&dev->lock))
        return -ERESTARTSYS;

    entry = aesd_circular_buffer_find_entry_offset_for_fpos(&dev->buffer, *f_pos, &entry_offset_byte);

    if (entry) {
        bytes_to_copy = entry->size - entry_offset_byte;
        if (bytes_to_copy > count)
            bytes_to_copy = count;

        if (copy_to_user(buf, entry->buffptr + entry_offset_byte, bytes_to_copy)) {
            retval = -EFAULT;
        } else {
            retval = bytes_to_copy;
            *f_pos += bytes_to_copy;
        }
    }

    mutex_unlock(&dev->lock);
    return retval;
}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval = -ENOMEM;
    struct aesd_dev *dev = filp->private_data;
    char *new_buffptr;
    const char *overwritten_ptr = NULL;

    PDEBUG("write %zu bytes with offset %lld",count,*f_pos);

    if (mutex_lock_interruptible(&dev->lock))
        return -ERESTARTSYS;

    new_buffptr = krealloc(dev->working_entry.buffptr, dev->working_entry.size + count, GFP_KERNEL);
    if (!new_buffptr) {
        goto out;
    }

    if (copy_from_user(new_buffptr + dev->working_entry.size, buf, count)) {
        retval = -EFAULT;
        dev->working_entry.buffptr = new_buffptr;
        goto out;
    }

    dev->working_entry.buffptr = new_buffptr;
    dev->working_entry.size += count;
    retval = count;

    if (memchr(dev->working_entry.buffptr + dev->working_entry.size - count, '\n', count)) {
        if (dev->buffer.full) {
            overwritten_ptr = dev->buffer.entry[dev->buffer.in_offs].buffptr;
        }
        
        aesd_circular_buffer_add_entry(&dev->buffer, &dev->working_entry);
        
        if (overwritten_ptr) {
            kfree(overwritten_ptr);
        }

        dev->working_entry.buffptr = NULL;
        dev->working_entry.size = 0;
    }

out:
    mutex_unlock(&dev->lock);
    return retval;
}

loff_t aesd_llseek(struct file *filp, loff_t off, int whence)
{
    struct aesd_dev *dev = filp->private_data;
    loff_t total_size = 0;
    uint8_t index;
    struct aesd_buffer_entry *entry;

    if (mutex_lock_interruptible(&dev->lock))
        return -ERESTARTSYS;

    AESD_CIRCULAR_BUFFER_FOREACH(entry, &dev->buffer, index) {
        total_size += entry->size;
    }

    mutex_unlock(&dev->lock);

    return fixed_size_llseek(filp, off, whence, total_size);
}

static long aesd_adjust_file_offset(struct file *filp, unsigned int write_cmd, unsigned int write_cmd_offset)
{
    struct aesd_dev *dev = filp->private_data;
    long retval = 0;
    uint8_t index;
    struct aesd_buffer_entry *entry;
    loff_t new_pos = 0;
    uint8_t count = 0;

    if (mutex_lock_interruptible(&dev->lock))
        return -ERESTARTSYS;

    // Check if write_cmd is valid
    AESD_CIRCULAR_BUFFER_FOREACH(entry, &dev->buffer, index) {
        if (entry->buffptr) count++;
    }

    if (write_cmd >= count) {
        retval = -EINVAL;
        goto out;
    }

    // Check if write_cmd_offset is valid within that entry
    // We need to find the actual entry by index. SLIST? No, it's a circular buffer.
    // The entries are from out_offs to in_offs.
    
    index = dev->buffer.out_offs;
    for (uint8_t i = 0; i < write_cmd; i++) {
        new_pos += dev->buffer.entry[index].size;
        index = (index + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
    }

    if (write_cmd_offset >= dev->buffer.entry[index].size) {
        retval = -EINVAL;
        goto out;
    }

    new_pos += write_cmd_offset;
    filp->f_pos = new_pos;

out:
    mutex_unlock(&dev->lock);
    return retval;
}

long aesd_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    long retval = 0;
    struct aesd_seekto seekto;

    if (_IOC_TYPE(cmd) != AESD_IOC_MAGIC) return -ENOTTY;
    if (_IOC_NR(cmd) > AESDCHAR_IOC_MAXNR) return -ENOTTY;

    switch (cmd) {
        case AESDCHAR_IOCSEEKTO:
            if (copy_from_user(&seekto, (struct aesd_seekto __user *)arg, sizeof(seekto))) {
                retval = -EFAULT;
            } else {
                retval = aesd_adjust_file_offset(filp, seekto.write_cmd, seekto.write_cmd_offset);
            }
            break;
        default:
            retval = -ENOTTY;
    }

    return retval;
}

struct file_operations aesd_fops = {
    .owner =    THIS_MODULE,
    .read =     aesd_read,
    .write =    aesd_write,
    .open =     aesd_open,
    .release =  aesd_release,
    .llseek =   aesd_llseek,
    .unlocked_ioctl = aesd_ioctl,
};

static int aesd_setup_cdev(struct aesd_dev *dev)
{
    int err, devno = MKDEV(aesd_major, aesd_minor);

    cdev_init(&dev->cdev, &aesd_fops);
    dev->cdev.owner = THIS_MODULE;
    dev->cdev.ops = &aesd_fops;
    err = cdev_add (&dev->cdev, devno, 1);
    if (err) {
        printk(KERN_ERR "Error %d adding aesd cdev", err);
    }
    return err;
}

int aesd_init_module(void)
{
    dev_t dev = 0;
    int result;
    result = alloc_chrdev_region(&dev, aesd_minor, 1,
            "aesdchar");
    aesd_major = MAJOR(dev);
    if (result < 0) {
        printk(KERN_WARNING "Can't get major %d\n", aesd_major);
        return result;
    }
    memset(&aesd_device,0,sizeof(struct aesd_dev));

    mutex_init(&aesd_device.lock);
    aesd_circular_buffer_init(&aesd_device.buffer);

    result = aesd_setup_cdev(&aesd_device);

    if( result ) {
        unregister_chrdev_region(dev, 1);
    }
    return result;

}

void aesd_cleanup_module(void)
{
    uint8_t index;
    struct aesd_buffer_entry *entry;
    dev_t devno = MKDEV(aesd_major, aesd_minor);

    cdev_del(&aesd_device.cdev);

    AESD_CIRCULAR_BUFFER_FOREACH(entry, &aesd_device.buffer, index) {
        if (entry->buffptr) {
            kfree(entry->buffptr);
        }
    }
    
    if (aesd_device.working_entry.buffptr) {
        kfree(aesd_device.working_entry.buffptr);
    }

    unregister_chrdev_region(devno, 1);
}

module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
