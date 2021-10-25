/*
 * vhba.c
 *
 * Copyright (C) 2007-2012 Chia-I Wu <olvaffe AT gmail DOT com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#define pr_fmt(fmt) "vhba: " fmt

#include <linux/version.h>

#include <linux/init.h>
#include <linux/module.h>
#include <linux/highmem.h>
#include <linux/fs.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 11, 0)
#include <linux/sched/signal.h>
#else
#include <linux/sched.h>
#endif
#include <linux/platform_device.h>
#include <linux/miscdevice.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/scatterlist.h>
#ifdef CONFIG_COMPAT
#include <linux/compat.h>
#endif
#include <asm/uaccess.h>
#include <scsi/scsi.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_tcq.h>


MODULE_AUTHOR("Chia-I Wu");
MODULE_VERSION(VHBA_VERSION);
MODULE_DESCRIPTION("Virtual SCSI HBA");
MODULE_LICENSE("GPL");


#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 15, 0)
#define sdev_dbg(sdev, fmt, a...) \
    dev_dbg(&(sdev)->sdev_gendev, fmt, ##a)
#define scmd_dbg(scmd, fmt, a...) \
    dev_dbg(&(scmd)->device->sdev_gendev, fmt, ##a)
#endif

#define VHBA_MAX_SECTORS_PER_IO 256
#define VHBA_MAX_BUS 16
#define VHBA_MAX_ID 16
#define VHBA_MAX_DEVICES (VHBA_MAX_BUS * (VHBA_MAX_ID-1))
#define VHBA_KBUF_SIZE PAGE_SIZE

#define DATA_TO_DEVICE(dir) ((dir) == DMA_TO_DEVICE || (dir) == DMA_BIDIRECTIONAL)
#define DATA_FROM_DEVICE(dir) ((dir) == DMA_FROM_DEVICE || (dir) == DMA_BIDIRECTIONAL)


static int vhba_can_queue = 32;
module_param_named(can_queue, vhba_can_queue, int, 0);


enum vhba_req_state {
    VHBA_REQ_FREE,
    VHBA_REQ_PENDING,
    VHBA_REQ_READING,
    VHBA_REQ_SENT,
    VHBA_REQ_WRITING,
};

struct vhba_command {
    struct scsi_cmnd *cmd;
    /* metatags are per-host. not to be confused with
       queue tags that are usually per-lun */
    unsigned long metatag;
    int status;
    struct list_head entry;
};

struct vhba_device {
    unsigned int num;
    spinlock_t cmd_lock;
    struct list_head cmd_list;
    wait_queue_head_t cmd_wq;
    atomic_t refcnt;

    unsigned char *kbuf;
    size_t kbuf_size;
};

struct vhba_host {
    struct Scsi_Host *shost;
    spinlock_t cmd_lock;
    int cmd_next;
    struct vhba_command *commands;
    spinlock_t dev_lock;
    struct vhba_device *devices[VHBA_MAX_DEVICES];
    int num_devices;
    DECLARE_BITMAP(chgmap, VHBA_MAX_DEVICES);
    int chgtype[VHBA_MAX_DEVICES];
    struct work_struct scan_devices;
};

#define MAX_COMMAND_SIZE 16

struct vhba_request {
    __u32 metatag;
    __u32 lun;
    __u8 cdb[MAX_COMMAND_SIZE];
    __u8 cdb_len;
    __u32 data_len;
};

struct vhba_response {
    __u32 metatag;
    __u32 status;
    __u32 data_len;
};



struct vhba_command *vhba_alloc_command (void);
void vhba_free_command (struct vhba_command *vcmd);

static struct platform_device vhba_platform_device;



/* These functions define a symmetric 1:1 mapping between device numbers and
   the bus and id. We have reserved the last id per bus for the host itself. */
void devnum_to_bus_and_id(unsigned int devnum, unsigned int *bus, unsigned int *id)
{
    *bus = devnum / (VHBA_MAX_ID-1);
    *id  = devnum % (VHBA_MAX_ID-1);
}

unsigned int bus_and_id_to_devnum(unsigned int bus, unsigned int id)
{
    return (bus * (VHBA_MAX_ID-1)) + id;
}

struct vhba_device *vhba_device_alloc (void)
{
    struct vhba_device *vdev;

    vdev = kzalloc(sizeof(struct vhba_device), GFP_KERNEL);
    if (!vdev) {
        return NULL;
    }

    spin_lock_init(&vdev->cmd_lock);
    INIT_LIST_HEAD(&vdev->cmd_list);
    init_waitqueue_head(&vdev->cmd_wq);
    atomic_set(&vdev->refcnt, 1);

    vdev->kbuf = NULL;
    vdev->kbuf_size = 0;

    return vdev;
}

void vhba_device_put (struct vhba_device *vdev)
{
    if (atomic_dec_and_test(&vdev->refcnt)) {
        kfree(vdev);
    }
}

struct vhba_device *vhba_device_get (struct vhba_device *vdev)
{
    atomic_inc(&vdev->refcnt);

    return vdev;
}

int vhba_device_queue (struct vhba_device *vdev, struct scsi_cmnd *cmd)
{
    struct vhba_host *vhost;
    struct vhba_command *vcmd;
    unsigned long flags;

    vhost = platform_get_drvdata(&vhba_platform_device);

    vcmd = vhba_alloc_command();
    if (!vcmd) {
        return SCSI_MLQUEUE_HOST_BUSY;
    }

    vcmd->cmd = cmd;

    spin_lock_irqsave(&vdev->cmd_lock, flags);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0)
    vcmd->metatag = scsi_cmd_to_rq(vcmd->cmd)->tag;
#else
    vcmd->metatag = vcmd->cmd->request->tag;
#endif
    list_add_tail(&vcmd->entry, &vdev->cmd_list);
    spin_unlock_irqrestore(&vdev->cmd_lock, flags);

    wake_up_interruptible(&vdev->cmd_wq);

    return 0;
}

int vhba_device_dequeue (struct vhba_device *vdev, struct scsi_cmnd *cmd)
{
    struct vhba_command *vcmd;
    int retval;
    unsigned long flags;

    spin_lock_irqsave(&vdev->cmd_lock, flags);
    list_for_each_entry(vcmd, &vdev->cmd_list, entry) {
        if (vcmd->cmd == cmd) {
            list_del_init(&vcmd->entry);
            break;
        }
    }

    /* command not found */
    if (&vcmd->entry == &vdev->cmd_list) {
        spin_unlock_irqrestore(&vdev->cmd_lock, flags);
        return SUCCESS;
    }

    while (vcmd->status == VHBA_REQ_READING || vcmd->status == VHBA_REQ_WRITING) {
        spin_unlock_irqrestore(&vdev->cmd_lock, flags);
        scmd_dbg(cmd, "wait for I/O before aborting\n");
        schedule_timeout(1);
        spin_lock_irqsave(&vdev->cmd_lock, flags);
    }

    retval = (vcmd->status == VHBA_REQ_SENT) ? FAILED : SUCCESS;

    vhba_free_command(vcmd);

    spin_unlock_irqrestore(&vdev->cmd_lock, flags);

    return retval;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 19, 0)
int vhba_slave_alloc(struct scsi_device *sdev)
{
    struct Scsi_Host *shost = sdev->host;

    sdev_dbg(sdev, "enabling tagging (queue depth: %i).\n", sdev->queue_depth);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 17, 0)
    if (!shost_use_blk_mq(shost) && shost->bqt) {
#else
    if (shost->bqt) {
#endif
        blk_queue_init_tags(sdev->request_queue, sdev->queue_depth, shost->bqt);
    }
    scsi_adjust_queue_depth(sdev, 0, sdev->queue_depth);

    return 0;
}
#endif

void vhba_scan_devices_add (struct vhba_host *vhost, int bus, int id)
{
    struct scsi_device *sdev;

    sdev = scsi_device_lookup(vhost->shost, bus, id, 0);
    if (!sdev) {
        scsi_add_device(vhost->shost, bus, id, 0);
    } else {
        dev_warn(&vhost->shost->shost_gendev, "tried to add an already-existing device %d:%d:0!\n", bus, id);
        scsi_device_put(sdev);
    }
}

void vhba_scan_devices_remove (struct vhba_host *vhost, int bus, int id)
{
    struct scsi_device *sdev;

    sdev = scsi_device_lookup(vhost->shost, bus, id, 0);
    if (sdev) {
        scsi_remove_device(sdev);
        scsi_device_put(sdev);
    } else {
        dev_warn(&vhost->shost->shost_gendev, "tried to remove non-existing device %d:%d:0!\n", bus, id);
    }
}

void vhba_scan_devices (struct work_struct *work)
{
    struct vhba_host *vhost = container_of(work, struct vhba_host, scan_devices);
    unsigned long flags;
    int change, exists;
    unsigned int devnum;
    unsigned int bus, id;

    for (;;) {
        spin_lock_irqsave(&vhost->dev_lock, flags);

        devnum = find_first_bit(vhost->chgmap, VHBA_MAX_DEVICES);
        if (devnum >= VHBA_MAX_DEVICES) {
            spin_unlock_irqrestore(&vhost->dev_lock, flags);
            break;
        }
        change = vhost->chgtype[devnum];
        exists = vhost->devices[devnum] != NULL;

        vhost->chgtype[devnum] = 0;
        clear_bit(devnum, vhost->chgmap);

        spin_unlock_irqrestore(&vhost->dev_lock, flags);

        devnum_to_bus_and_id(devnum, &bus, &id);

        if (change < 0) {
            dev_dbg(&vhost->shost->shost_gendev, "trying to remove target %d:%d:0\n", bus, id);
            vhba_scan_devices_remove(vhost, bus, id);
        } else if (change > 0) {
            dev_dbg(&vhost->shost->shost_gendev, "trying to add target %d:%d:0\n", bus, id);
            vhba_scan_devices_add(vhost, bus, id);
        } else {
            /* quick sequence of add/remove or remove/add; we determine
               which one it was by checking if device structure exists */
            if (exists) {
                /* remove followed by add: remove and (re)add */
                dev_dbg(&vhost->shost->shost_gendev, "trying to (re)add target %d:%d:0\n", bus, id);
                vhba_scan_devices_remove(vhost, bus, id);
                vhba_scan_devices_add(vhost, bus, id);
            } else {
                /* add followed by remove: no-op */
                dev_dbg(&vhost->shost->shost_gendev, "no-op for target %d:%d:0\n", bus, id);
            }
        }
    }
}

int vhba_add_device (struct vhba_device *vdev)
{
    struct vhba_host *vhost;
    unsigned int devnum;
    unsigned long flags;

    vhost = platform_get_drvdata(&vhba_platform_device);

    vhba_device_get(vdev);

    spin_lock_irqsave(&vhost->dev_lock, flags);
    if (vhost->num_devices >= VHBA_MAX_DEVICES) {
        spin_unlock_irqrestore(&vhost->dev_lock, flags);
        vhba_device_put(vdev);
        return -EBUSY;
    }

    for (devnum = 0; devnum < VHBA_MAX_DEVICES; devnum++) {
        if (vhost->devices[devnum] == NULL) {
            vdev->num = devnum;
            vhost->devices[devnum] = vdev;
            vhost->num_devices++;
            set_bit(devnum, vhost->chgmap);
            vhost->chgtype[devnum]++;
            break;
        }
    }
    spin_unlock_irqrestore(&vhost->dev_lock, flags);

    schedule_work(&vhost->scan_devices);

    return 0;
}

int vhba_remove_device (struct vhba_device *vdev)
{
    struct vhba_host *vhost;
    unsigned long flags;

    vhost = platform_get_drvdata(&vhba_platform_device);

    spin_lock_irqsave(&vhost->dev_lock, flags);
    set_bit(vdev->num, vhost->chgmap);
    vhost->chgtype[vdev->num]--;
    vhost->devices[vdev->num] = NULL;
    vhost->num_devices--;
    spin_unlock_irqrestore(&vhost->dev_lock, flags);

    vhba_device_put(vdev);

    schedule_work(&vhost->scan_devices);

    return 0;
}

struct vhba_device *vhba_lookup_device (int devnum)
{
    struct vhba_host *vhost;
    struct vhba_device *vdev = NULL;
    unsigned long flags;

    vhost = platform_get_drvdata(&vhba_platform_device);

    if (likely(devnum < VHBA_MAX_DEVICES)) {
        spin_lock_irqsave(&vhost->dev_lock, flags);
        vdev = vhost->devices[devnum];
        if (vdev) {
            vdev = vhba_device_get(vdev);
        }

        spin_unlock_irqrestore(&vhost->dev_lock, flags);
    }

    return vdev;
}

struct vhba_command *vhba_alloc_command (void)
{
    struct vhba_host *vhost;
    struct vhba_command *vcmd;
    unsigned long flags;
    int i;

    vhost = platform_get_drvdata(&vhba_platform_device);

    spin_lock_irqsave(&vhost->cmd_lock, flags);

    vcmd = vhost->commands + vhost->cmd_next++;
    if (vcmd->status != VHBA_REQ_FREE) {
        for (i = 0; i < vhba_can_queue; i++) {
            vcmd = vhost->commands + i;

            if (vcmd->status == VHBA_REQ_FREE) {
                vhost->cmd_next = i + 1;
                break;
            }
        }

        if (i == vhba_can_queue) {
            vcmd = NULL;
        }
    }

    if (vcmd) {
        vcmd->status = VHBA_REQ_PENDING;
    }

    vhost->cmd_next %= vhba_can_queue;

    spin_unlock_irqrestore(&vhost->cmd_lock, flags);

    return vcmd;
}

void vhba_free_command (struct vhba_command *vcmd)
{
    struct vhba_host *vhost;
    unsigned long flags;

    vhost = platform_get_drvdata(&vhba_platform_device);

    spin_lock_irqsave(&vhost->cmd_lock, flags);
    vcmd->status = VHBA_REQ_FREE;
    spin_unlock_irqrestore(&vhost->cmd_lock, flags);
}

int vhba_queuecommand (struct Scsi_Host *shost, struct scsi_cmnd *cmd)
{
    struct vhba_device *vdev;
    int retval;
    unsigned int devnum;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0)
    scmd_dbg(cmd, "queue %p tag %i\n", cmd, scsi_cmd_to_rq(cmd)->tag);
#else
    scmd_dbg(cmd, "queue %p tag %i\n", cmd, cmd->request->tag);
#endif

    devnum = bus_and_id_to_devnum(cmd->device->channel, cmd->device->id);
    vdev = vhba_lookup_device(devnum);
    if (!vdev) {
        scmd_dbg(cmd, "no such device\n");

        cmd->result = DID_NO_CONNECT << 16;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0)
        scsi_done(cmd);
#else
        cmd->scsi_done(cmd);
#endif

        return 0;
    }

    retval = vhba_device_queue(vdev, cmd);

    vhba_device_put(vdev);

    return retval;
}

int vhba_abort (struct scsi_cmnd *cmd)
{
    struct vhba_device *vdev;
    int retval = SUCCESS;
    unsigned int devnum;

    scmd_dbg(cmd, "abort %p\n", cmd);

    devnum = bus_and_id_to_devnum(cmd->device->channel, cmd->device->id);
    vdev = vhba_lookup_device(devnum);
    if (vdev) {
        retval = vhba_device_dequeue(vdev, cmd);
        vhba_device_put(vdev);
    } else {
        cmd->result = DID_NO_CONNECT << 16;
    }

    return retval;
}

static struct scsi_host_template vhba_template = {
    .module = THIS_MODULE,
    .name = "vhba",
    .proc_name = "vhba",
    .queuecommand = vhba_queuecommand,
    .eh_abort_handler = vhba_abort,
    .this_id = -1,
    .max_sectors = VHBA_MAX_SECTORS_PER_IO,
    .sg_tablesize = 256,
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 19, 0)
    .slave_alloc = vhba_slave_alloc,
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 0, 0)
    .tag_alloc_policy = BLK_TAG_ALLOC_RR,
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 19, 0) && LINUX_VERSION_CODE < KERNEL_VERSION(4, 4, 0)
    .use_blk_tags = 1,
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 0, 0)
    .max_segment_size = VHBA_KBUF_SIZE,
#endif
};

ssize_t do_request (struct vhba_device *vdev, unsigned long metatag, struct scsi_cmnd *cmd, char __user *buf, size_t buf_len)
{
    struct vhba_request vreq;
    ssize_t ret;

    scmd_dbg(cmd, "request %lu (%p), cdb 0x%x, bufflen %d, sg count %d\n",
        metatag, cmd, cmd->cmnd[0], scsi_bufflen(cmd), scsi_sg_count(cmd));

    ret = sizeof(vreq);
    if (DATA_TO_DEVICE(cmd->sc_data_direction)) {
        ret += scsi_bufflen(cmd);
    }

    if (ret > buf_len) {
        scmd_dbg(cmd, "buffer too small (%zd < %zd) for a request\n", buf_len, ret);
        return -EIO;
    }

    vreq.metatag = metatag;
    vreq.lun = cmd->device->lun;
    memcpy(vreq.cdb, cmd->cmnd, MAX_COMMAND_SIZE);
    vreq.cdb_len = cmd->cmd_len;
    vreq.data_len = scsi_bufflen(cmd);

    if (copy_to_user(buf, &vreq, sizeof(vreq))) {
        return -EFAULT;
    }

    if (DATA_TO_DEVICE(cmd->sc_data_direction) && vreq.data_len) {
        buf += sizeof(vreq);

        if (scsi_sg_count(cmd)) {
            unsigned char *kaddr, *uaddr;
            struct scatterlist *sglist = scsi_sglist(cmd);
            struct scatterlist *sg;
            int i;

            uaddr = (unsigned char *) buf;

            for_each_sg(sglist, sg, scsi_sg_count(cmd), i) {
                size_t len = sg->length;

                if (len > vdev->kbuf_size) {
                    scmd_dbg(cmd, "segment size (%zu) exceeds kbuf size (%zu)!", len, vdev->kbuf_size);
                    len = vdev->kbuf_size;
                }

                kaddr = kmap_atomic(sg_page(sg));
                memcpy(vdev->kbuf, kaddr + sg->offset, len);
                kunmap_atomic(kaddr);

                if (copy_to_user(uaddr, vdev->kbuf, len)) {
                    return -EFAULT;
                }
                uaddr += len;
            }
        } else {
            if (copy_to_user(buf, scsi_sglist(cmd), vreq.data_len)) {
                return -EFAULT;
            }
        }
    }

    return ret;
}

ssize_t do_response (struct vhba_device *vdev, unsigned long metatag, struct scsi_cmnd *cmd, const char __user *buf, size_t buf_len, struct vhba_response *res)
{
    ssize_t ret = 0;

    scmd_dbg(cmd, "response %lu (%p), status %x, data len %d, sg count %d\n",
         metatag, cmd, res->status, res->data_len, scsi_sg_count(cmd));

    if (res->status) {
        if (res->data_len > SCSI_SENSE_BUFFERSIZE) {
            scmd_dbg(cmd, "truncate sense (%d < %d)", SCSI_SENSE_BUFFERSIZE, res->data_len);
            res->data_len = SCSI_SENSE_BUFFERSIZE;
        }

        if (copy_from_user(cmd->sense_buffer, buf, res->data_len)) {
            return -EFAULT;
        }

        cmd->result = res->status;

        ret += res->data_len;
    } else if (DATA_FROM_DEVICE(cmd->sc_data_direction) && scsi_bufflen(cmd)) {
        size_t to_read;

        if (res->data_len > scsi_bufflen(cmd)) {
            scmd_dbg(cmd, "truncate data (%d < %d)\n", scsi_bufflen(cmd), res->data_len);
            res->data_len = scsi_bufflen(cmd);
        }

        to_read = res->data_len;

        if (scsi_sg_count(cmd)) {
            unsigned char *kaddr, *uaddr;
            struct scatterlist *sglist = scsi_sglist(cmd);
            struct scatterlist *sg;
            int i;

            uaddr = (unsigned char *)buf;

            for_each_sg(sglist, sg, scsi_sg_count(cmd), i) {
                size_t len = (sg->length < to_read) ? sg->length : to_read;

                if (len > vdev->kbuf_size) {
                    scmd_dbg(cmd, "segment size (%zu) exceeds kbuf size (%zu)!", len, vdev->kbuf_size);
                    len = vdev->kbuf_size;
                }

                if (copy_from_user(vdev->kbuf, uaddr, len)) {
                    return -EFAULT;
                }
                uaddr += len;

                kaddr = kmap_atomic(sg_page(sg));
                memcpy(kaddr + sg->offset, vdev->kbuf, len);
                kunmap_atomic(kaddr);

                to_read -= len;
                if (to_read == 0) {
                    break;
                }
            }
        } else {
            if (copy_from_user(scsi_sglist(cmd), buf, res->data_len)) {
                return -EFAULT;
            }

            to_read -= res->data_len;
        }

        scsi_set_resid(cmd, to_read);

        ret += res->data_len - to_read;
    }

    return ret;
}

struct vhba_command *next_command (struct vhba_device *vdev)
{
    struct vhba_command *vcmd;

    list_for_each_entry(vcmd, &vdev->cmd_list, entry) {
        if (vcmd->status == VHBA_REQ_PENDING) {
            break;
        }
    }

    if (&vcmd->entry == &vdev->cmd_list) {
        vcmd = NULL;
    }

    return vcmd;
}

struct vhba_command *match_command (struct vhba_device *vdev, __u32 metatag)
{
    struct vhba_command *vcmd;

    list_for_each_entry(vcmd, &vdev->cmd_list, entry) {
        if (vcmd->metatag == metatag) {
            break;
        }
    }

    if (&vcmd->entry == &vdev->cmd_list) {
        vcmd = NULL;
    }

    return vcmd;
}

struct vhba_command *wait_command (struct vhba_device *vdev, unsigned long flags)
{
    struct vhba_command *vcmd;
    DEFINE_WAIT(wait);

    while (!(vcmd = next_command(vdev))) {
        if (signal_pending(current)) {
            break;
        }

        prepare_to_wait(&vdev->cmd_wq, &wait, TASK_INTERRUPTIBLE);

        spin_unlock_irqrestore(&vdev->cmd_lock, flags);

        schedule();

        spin_lock_irqsave(&vdev->cmd_lock, flags);
    }

    finish_wait(&vdev->cmd_wq, &wait);
    if (vcmd) {
        vcmd->status = VHBA_REQ_READING;
    }

    return vcmd;
}

ssize_t vhba_ctl_read (struct file *file, char __user *buf, size_t buf_len, loff_t *offset)
{
    struct vhba_device *vdev;
    struct vhba_command *vcmd;
    ssize_t ret;
    unsigned long flags;

    vdev = file->private_data;

    /* Get next command */
    if (file->f_flags & O_NONBLOCK) {
        /* Non-blocking variant */
        spin_lock_irqsave(&vdev->cmd_lock, flags);
        vcmd = next_command(vdev);
        spin_unlock_irqrestore(&vdev->cmd_lock, flags);

        if (!vcmd) {
            return -EWOULDBLOCK;
        }
    } else {
        /* Blocking variant */
        spin_lock_irqsave(&vdev->cmd_lock, flags);
        vcmd = wait_command(vdev, flags);
        spin_unlock_irqrestore(&vdev->cmd_lock, flags);

        if (!vcmd) {
            return -ERESTARTSYS;
        }
    }

    ret = do_request(vdev, vcmd->metatag, vcmd->cmd, buf, buf_len);

    spin_lock_irqsave(&vdev->cmd_lock, flags);
    if (ret >= 0) {
        vcmd->status = VHBA_REQ_SENT;
        *offset += ret;
    } else {
        vcmd->status = VHBA_REQ_PENDING;
    }

    spin_unlock_irqrestore(&vdev->cmd_lock, flags);

    return ret;
}

ssize_t vhba_ctl_write (struct file *file, const char __user *buf, size_t buf_len, loff_t *offset)
{
    struct vhba_device *vdev;
    struct vhba_command *vcmd;
    struct vhba_response res;
    ssize_t ret;
    unsigned long flags;

    if (buf_len < sizeof(res)) {
        return -EIO;
    }

    if (copy_from_user(&res, buf, sizeof(res))) {
        return -EFAULT;
    }

    vdev = file->private_data;

    spin_lock_irqsave(&vdev->cmd_lock, flags);
    vcmd = match_command(vdev, res.metatag);
    if (!vcmd || vcmd->status != VHBA_REQ_SENT) {
        spin_unlock_irqrestore(&vdev->cmd_lock, flags);
        pr_debug("ctl dev #%u not expecting response\n", vdev->num);
        return -EIO;
    }
    vcmd->status = VHBA_REQ_WRITING;
    spin_unlock_irqrestore(&vdev->cmd_lock, flags);

    ret = do_response(vdev, vcmd->metatag, vcmd->cmd, buf + sizeof(res), buf_len - sizeof(res), &res);

    spin_lock_irqsave(&vdev->cmd_lock, flags);
    if (ret >= 0) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0)
        scsi_done(vcmd->cmd);
#else
        vcmd->cmd->scsi_done(vcmd->cmd);
#endif
        ret += sizeof(res);

        /* don't compete with vhba_device_dequeue */
        if (!list_empty(&vcmd->entry)) {
            list_del_init(&vcmd->entry);
            vhba_free_command(vcmd);
        }
    } else {
        vcmd->status = VHBA_REQ_SENT;
    }

    spin_unlock_irqrestore(&vdev->cmd_lock, flags);

    return ret;
}

long vhba_ctl_ioctl (struct file *file, unsigned int cmd, unsigned long arg)
{
    struct vhba_device *vdev = file->private_data;
    struct vhba_host *vhost = platform_get_drvdata(&vhba_platform_device);

    switch (cmd) {
        case 0xBEEF001: {
            unsigned int ident[4]; /* host, channel, id, lun */

            ident[0] = vhost->shost->host_no;
            devnum_to_bus_and_id(vdev->num, &ident[1], &ident[2]);
            ident[3] = 0; /* lun */

            if (copy_to_user((void *) arg, ident, sizeof(ident))) {
                return -EFAULT;
            }

            return 0;
        }
        case 0xBEEF002: {
            unsigned int devnum = vdev->num;

            if (copy_to_user((void *) arg, &devnum, sizeof(devnum))) {
                return -EFAULT;
            }

            return 0;
        }
    }

    return -ENOTTY;
}

#ifdef CONFIG_COMPAT
long vhba_ctl_compat_ioctl (struct file *file, unsigned int cmd, unsigned long arg)
{
    unsigned long compat_arg = (unsigned long)compat_ptr(arg);
    return vhba_ctl_ioctl(file, cmd, compat_arg);
}
#endif

unsigned int vhba_ctl_poll (struct file *file, poll_table *wait)
{
    struct vhba_device *vdev = file->private_data;
    unsigned int mask = 0;
    unsigned long flags;

    poll_wait(file, &vdev->cmd_wq, wait);

    spin_lock_irqsave(&vdev->cmd_lock, flags);
    if (next_command(vdev)) {
        mask |= POLLIN | POLLRDNORM;
    }
    spin_unlock_irqrestore(&vdev->cmd_lock, flags);

    return mask;
}

int vhba_ctl_open (struct inode *inode, struct file *file)
{
    struct vhba_device *vdev;
    int retval;

    pr_debug("ctl dev open\n");

    /* check if vhba is probed */
    if (!platform_get_drvdata(&vhba_platform_device)) {
        return -ENODEV;
    }

    vdev = vhba_device_alloc();
    if (!vdev) {
        return -ENOMEM;
    }

    vdev->kbuf_size = VHBA_KBUF_SIZE;
    vdev->kbuf = kzalloc(vdev->kbuf_size, GFP_KERNEL);
    if (!vdev->kbuf) {
        return -ENOMEM;
    }

    if (!(retval = vhba_add_device(vdev))) {
        file->private_data = vdev;
    }

    vhba_device_put(vdev);

    return retval;
}

int vhba_ctl_release (struct inode *inode, struct file *file)
{
    struct vhba_device *vdev;
    struct vhba_command *vcmd;
    unsigned long flags;

    vdev = file->private_data;

    pr_debug("ctl dev release\n");

    vhba_device_get(vdev);
    vhba_remove_device(vdev);

    spin_lock_irqsave(&vdev->cmd_lock, flags);
    list_for_each_entry(vcmd, &vdev->cmd_list, entry) {
        WARN_ON(vcmd->status == VHBA_REQ_READING || vcmd->status == VHBA_REQ_WRITING);

        scmd_dbg(vcmd->cmd, "device released with command %lu (%p)\n", vcmd->metatag, vcmd->cmd);
        vcmd->cmd->result = DID_NO_CONNECT << 16;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0)
        scsi_done(vcmd->cmd);
#else
        vcmd->cmd->scsi_done(vcmd->cmd);
#endif
        vhba_free_command(vcmd);
    }
    INIT_LIST_HEAD(&vdev->cmd_list);
    spin_unlock_irqrestore(&vdev->cmd_lock, flags);

    kfree(vdev->kbuf);
    vdev->kbuf = NULL;

    vhba_device_put(vdev);

    return 0;
}

static struct file_operations vhba_ctl_fops = {
    .owner = THIS_MODULE,
    .open = vhba_ctl_open,
    .release = vhba_ctl_release,
    .read = vhba_ctl_read,
    .write = vhba_ctl_write,
    .poll = vhba_ctl_poll,
    .unlocked_ioctl = vhba_ctl_ioctl,
#ifdef CONFIG_COMPAT
    .compat_ioctl = vhba_ctl_compat_ioctl,
#endif
};

static struct miscdevice vhba_miscdev = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = "vhba_ctl",
    .fops = &vhba_ctl_fops,
};

int vhba_probe (struct platform_device *pdev)
{
    struct Scsi_Host *shost;
    struct vhba_host *vhost;
    int i;

    vhba_can_queue = clamp(vhba_can_queue, 1, 256);

    shost = scsi_host_alloc(&vhba_template, sizeof(struct vhba_host));
    if (!shost) {
        return -ENOMEM;
    }

    shost->max_channel = VHBA_MAX_BUS-1;
    shost->max_id = VHBA_MAX_ID;
    /* we don't support lun > 0 */
    shost->max_lun = 1;
    shost->max_cmd_len = MAX_COMMAND_SIZE;
    shost->can_queue = vhba_can_queue;
    shost->cmd_per_lun = vhba_can_queue;

    vhost = (struct vhba_host *)shost->hostdata;
    memset(vhost, 0, sizeof(struct vhba_host));

    vhost->shost = shost;
    vhost->num_devices = 0;
    spin_lock_init(&vhost->dev_lock);
    spin_lock_init(&vhost->cmd_lock);
    INIT_WORK(&vhost->scan_devices, vhba_scan_devices);
    vhost->cmd_next = 0;
    vhost->commands = kzalloc(vhba_can_queue * sizeof(struct vhba_command), GFP_KERNEL);
    if (!vhost->commands) {
        return -ENOMEM;
    }

    for (i = 0; i < vhba_can_queue; i++) {
        vhost->commands[i].status = VHBA_REQ_FREE;
    }

    platform_set_drvdata(pdev, vhost);

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 4, 0)
    i = scsi_init_shared_tag_map(shost, vhba_can_queue);
    if (i) return i;
#endif

    if (scsi_add_host(shost, &pdev->dev)) {
        scsi_host_put(shost);
        return -ENOMEM;
    }

    return 0;
}

int vhba_remove (struct platform_device *pdev)
{
    struct vhba_host *vhost;
    struct Scsi_Host *shost;

    vhost = platform_get_drvdata(pdev);
    shost = vhost->shost;

    scsi_remove_host(shost);
    scsi_host_put(shost);

    kfree(vhost->commands);

    return 0;
}

void vhba_release (struct device * dev)
{
    return;
}

static struct platform_device vhba_platform_device = {
    .name = "vhba",
    .id = -1,
    .dev = {
        .release = vhba_release,
    },
};

static struct platform_driver vhba_platform_driver = {
    .driver = {
        .owner = THIS_MODULE,
        .name = "vhba",
    },
    .probe = vhba_probe,
    .remove = vhba_remove,
};

int __init vhba_init (void)
{
    int ret;

    ret = platform_device_register(&vhba_platform_device);
    if (ret < 0) {
        return ret;
    }

    ret = platform_driver_register(&vhba_platform_driver);
    if (ret < 0) {
        platform_device_unregister(&vhba_platform_device);
        return ret;
    }

    ret = misc_register(&vhba_miscdev);
    if (ret < 0) {
        platform_driver_unregister(&vhba_platform_driver);
        platform_device_unregister(&vhba_platform_device);
        return ret;
    }

    return 0;
}

void __exit vhba_exit(void)
{
    misc_deregister(&vhba_miscdev);
    platform_driver_unregister(&vhba_platform_driver);
    platform_device_unregister(&vhba_platform_device);
}

module_init(vhba_init);
module_exit(vhba_exit);

