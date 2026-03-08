#include <linux/sched.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/time.h>
#include <linux/types.h>
#include <linux/ioctl.h>
#include <linux/proc_fs.h>
#include <linux/idme_t.h>
#include <asm/ioctls.h>

#include "dummy_idme.h"

/* /dev/idme user space provisioning interface */
static int idme_open(struct inode *inode, struct file *file);
static ssize_t idme_write(struct file *, const char __user *, size_t, loff_t *);

struct dummy_idme {
	struct miscdevice	misc;	/* misc device representing the log */
	struct idme_ftr     idme;
};

static const struct file_operations idme_fops = {
	.owner = THIS_MODULE,
	.write = idme_write,
	.open = idme_open,
};

static struct dummy_idme idme = {
	.idme = {
		.serial = "7094181251971VVD",
		.board_id = "06318071519500NC",
		.mac_addr = "",
		.mac_sec = "5WYV647OCJ94JFS5F5A7",
		.bt_mac_addr = "",
		.bootmode = 1,
		.postmode = 0,
		.boot_count = 1,
		.productid = "0",
		.manufacturing = "",
		.productid2 = "",
		.unlock_code = "XXXX",
		.fos_flags = 0x23ull,
		.dev_flags = 0x0ull,
		.usr_flags = 0x0ull,
		.device_type_id = "ADVBD696BHNV5",
	},
	.misc = {
		.minor = MISC_DYNAMIC_MINOR,
		.name = "idme",
		.fops = &idme_fops,
		.parent = NULL,
	},
};

static int idme_open(struct inode *inode, struct file *file)
{
	file->private_data = &idme;
	return 0;
}

static ssize_t idme_write(struct file * file, const char __user * user, size_t size, loff_t * of)
{
	struct dummy_idme * d_idme = file->private_data;
	struct idme_ftr *idme = &(d_idme->idme);
	struct idme_ftr *idme_user = (struct idme_ftr*)user;
	int un_cpy_size = 0;

	if (IDME_MAGIC == idme_user->magic) {
		un_cpy_size = copy_from_user(idme, user, size);   
	}

	return size - un_cpy_size;
}

/*
static int proc_set_value(char* page, const char* proc_value, int *eof) {
	if (strlen(proc_value) < PAGE_SIZE){
	    memcpy(page, proc_value, strlen(proc_value));
	    *eof = 1;
	    return strlen(proc_value);
	}
	return 0;
}

static int proc_bootcount_read(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	char num[16] = {0};
	snprintf(num, sizeof(num), "%d", (__le32)(*((__le32*) data)));
	return proc_set_value(page, num, eof);
}

#define PROC_ID_READ(id) proc_id_read(page, start, off, count, eof, data, id)

static int proc_serial_read(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	return proc_set_value(page,  (const char*)data, eof);
}

static int proc_board_id_read(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	return proc_set_value(page,  (const char*)data, eof);
}

static int proc_mac_address_read(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	return proc_set_value(page,  (const char*)data, eof);
}

static int proc_bt_mac_address_read(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	return proc_set_value(page,  (const char*)data, eof);
}

static int proc_mac_secret_read(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	return proc_set_value(page,  (const char*)data, eof);
}

static int proc_bootmode_read(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	char num[16] = {0};
	snprintf(num, sizeof(num), "%d", (__le16)(*((__le16*) data)));
	return proc_set_value(page, num, eof);
}

static int proc_postmode_read(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	char num[16] = {0};
	snprintf(num, sizeof(num), "%d", (__le16)(*((__le16*) data)));
	return proc_set_value(page, num, eof);
}

static int proc_pfeature_wan_name_func(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	return proc_set_value(page, DUMMY_VALUE_WAN, eof);
}

static int proc_productid_read(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	return proc_set_value(page, (const char*)data, eof);
}

static int proc_manifacture_read(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	return proc_set_value(page, (const char*)data, eof);
}

static int proc_panelcal_read(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	return proc_set_value(page, (const char*)data, eof);
}

static int proc_productid2_read(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	return proc_set_value(page, (const char*)data, eof);
}

static int proc_time_offset_read(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	char num[16] = {0};
	snprintf(num, sizeof(num), "%d", (__le32)(*((__le32*) data)));
	return proc_set_value(page, num, eof);
}

static int proc_unlock_code_read(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	return proc_set_value(page, (const char*)data, eof);
}

static bool board_has_wan(void)
{
	//TODO detect board type
	return 1;
}

static int __init idme_init(void)
{
	struct proc_dir_entry *proc_idme = NULL;
	static struct proc_dir_entry *proc_product_features_dir;

	proc_idme = proc_mkdir(IDME_DIRECTORY, NULL);
	if (!proc_idme) {
	    return -ENOMEM;
	}

	if (NULL != proc_idme) {
	    struct proc_dir_entry *proc_serial = create_proc_entry(IDME_PROCNAME_SERIAL, S_IRUGO, proc_idme);
	    struct proc_dir_entry *proc_board_id = create_proc_entry(IDME_PROCNAME_BOARDID, S_IRUGO, proc_idme);
	    struct proc_dir_entry *proc_mac_address = create_proc_entry(IDME_PROCNAME_MACADDR, S_IRUGO, proc_idme);
	    struct proc_dir_entry *proc_mac_secret = create_proc_entry(IDME_PROCNAME_MACSEC, S_IRUGO, proc_idme);
	    struct proc_dir_entry *proc_bt_mac_address = create_proc_entry(IDME_PROCNAME_BTMACADDR, S_IRUGO, proc_idme);
	    struct proc_dir_entry *proc_bootmode = create_proc_entry(IDME_PROCNAME_BOOTMODE, S_IRUGO, proc_idme);
	    struct proc_dir_entry *proc_postmode = create_proc_entry(IDME_PROCNAME_POSTMODE, S_IRUGO, proc_idme);
	    struct proc_dir_entry *proc_bootcount = create_proc_entry(IDME_PROCNAME_BOOTCOUNT, S_IRUGO, proc_idme);
	    struct proc_dir_entry *proc_productid = create_proc_entry(IDME_PROCNAME_PRODUCTID, S_IRUGO, proc_idme);
	    struct proc_dir_entry *proc_manifacture = create_proc_entry(IDME_PROCNAME_MANUFACTURING, S_IRUGO, proc_idme);
	    struct proc_dir_entry *proc_panelcal = create_proc_entry(IDME_PROCNAME_PANELCAL, S_IRUGO, proc_idme);
	    struct proc_dir_entry *proc_productid2 = create_proc_entry(IDME_PROCNAME_PRODUCTID2, S_IRUGO, proc_idme);
	    struct proc_dir_entry *proc_time_offset = create_proc_entry(IDME_PROCNAME_TIME_OFFSET, S_IRUGO, proc_idme);
	    struct proc_dir_entry *proc_unlock_code = create_proc_entry(IDME_PROCNAME_UNLOCK_CODE, S_IRUGO, proc_idme);

	    if (proc_serial != NULL) {
	        proc_serial->data = idme.idme.serial;
	        proc_serial->read_proc = proc_serial_read;
	        proc_serial->write_proc = NULL;
	    }

	    if (proc_board_id != NULL) {
	        proc_board_id->data = idme.idme.board_id;
	        proc_board_id->read_proc = proc_board_id_read;
	        proc_board_id->write_proc = NULL;
	    }

	    if (proc_mac_address != NULL) {
	        proc_mac_address->data = idme.idme.mac_addr;
	        proc_mac_address->read_proc = proc_mac_address_read;
	        proc_mac_address->write_proc = NULL;
	    }

	    if (proc_mac_secret != NULL) {
	        proc_mac_secret->data = idme.idme.mac_sec;
	        proc_mac_secret->read_proc = proc_mac_secret_read;
	        proc_mac_secret->write_proc = NULL;
	    }

	    if (proc_bt_mac_address != NULL) {
	        proc_bt_mac_address->data = idme.idme.bt_mac_addr;
	        proc_bt_mac_address->read_proc = proc_bt_mac_address_read;
	        proc_bt_mac_address->write_proc = NULL;
	    }

	    if (proc_bootmode != NULL) {
	        proc_bootmode->data = &(idme.idme.bootmode);
	        proc_bootmode->read_proc = proc_bootmode_read;
	        proc_bootmode->write_proc = NULL;
	    }

	    if (proc_postmode != NULL) {
	        proc_postmode->data = &(idme.idme.postmode);
	        proc_postmode->read_proc = proc_postmode_read;
	        proc_postmode->write_proc = NULL;
	    }

	    if (proc_bootcount != NULL) {
	        proc_bootcount->data = &(idme.idme.boot_count);
	        proc_bootcount->read_proc = proc_bootcount_read;
	        proc_bootcount->write_proc = NULL;
	    }

	    if (proc_productid != NULL) {
	        proc_productid->data = idme.idme.productid;
	        proc_productid->read_proc = proc_productid_read;
	        proc_productid->write_proc = NULL;
	    }

	    if (proc_manifacture != NULL) {
	        proc_manifacture->data = idme.idme.manufacturing;
	        proc_manifacture->read_proc = proc_manifacture_read;
	        proc_manifacture->write_proc = NULL;
	    }

	    if (proc_panelcal != NULL) {
	        proc_panelcal->data = idme.idme.panelcal;
	        proc_panelcal->read_proc = proc_panelcal_read;
	        proc_panelcal->write_proc = NULL;
	    }

	    if (proc_productid2 != NULL) {
	        proc_productid2->data = idme.idme.productid2;
	        proc_productid2->read_proc = proc_productid2_read;
	        proc_productid2->write_proc = NULL;
	    }

	    if (proc_time_offset != NULL) {
	        proc_time_offset->data = &(idme.idme.time_offset);
	        proc_time_offset->read_proc = proc_time_offset_read;
	        proc_time_offset->write_proc = NULL;
	    }

	    if (proc_unlock_code != NULL) {
	        proc_unlock_code->data = idme.idme.unlock_code;
	        proc_unlock_code->read_proc = proc_unlock_code_read;
	        proc_unlock_code->write_proc = NULL;
	    }
	}

	proc_product_features_dir = proc_mkdir("product_features", NULL);
	if (NULL != proc_product_features_dir) {
	    if (board_has_wan()) {
	        struct proc_dir_entry *proc_wan_name = create_proc_entry(PRODUCT_FEATURE_NAME_WAN, S_IRUGO, proc_product_features_dir);
	        if (proc_wan_name != NULL) {
	            proc_wan_name->data = NULL;
	            proc_wan_name->read_proc = proc_pfeature_wan_name_func;
	            proc_wan_name->write_proc = NULL;
	        }
	    }
	}

	return 0;
}


*/

static ssize_t idme_serial_read(struct file *file, char __user *buf, size_t size, loff_t *ppos)
{
	return simple_read_from_buffer(buf, size, ppos, idme.idme.serial, strlen(idme.idme.serial));
}
static const struct file_operations idme_serial_fops = {
	.read 		= idme_serial_read,
};


static ssize_t idme_board_id_read(struct file *file, char __user *buf, size_t size, loff_t *ppos)
{
	return simple_read_from_buffer(buf, size, ppos, idme.idme.board_id, strlen(idme.idme.board_id)); 
}
static const struct file_operations idme_board_id_fops = {
	.read 		= idme_board_id_read,
};


static ssize_t idme_mac_address_read(struct file *file, char __user *buf, size_t size, loff_t *ppos)
{
	return simple_read_from_buffer(buf, size, ppos, idme.idme.mac_addr, strlen(idme.idme.mac_addr)); 
}
static const struct file_operations idme_mac_address_fops = {
	.read           = idme_mac_address_read,
};


static ssize_t idme_mac_secret_read(struct file *file, char __user *buf, size_t size, loff_t *ppos)
{
	return simple_read_from_buffer(buf, size, ppos, idme.idme.mac_sec, strlen(idme.idme.mac_sec)); 
}
static const struct file_operations idme_mac_secret_fops = {
	.read           = idme_mac_secret_read,
};


static ssize_t idme_bt_mac_address_read(struct file *file, char __user *buf, size_t size, loff_t *ppos)
{
	return simple_read_from_buffer(buf, size, ppos, idme.idme.bt_mac_addr, strlen(idme.idme.bt_mac_addr)); 
}
static const struct file_operations idme_bt_mac_address_fops = {
	.read           = idme_bt_mac_address_read,
};

static ssize_t idme_bootmode_read(struct file *file, char __user *buf, size_t size, loff_t *ppos)
{
	char num[16] = {0};
	snprintf(num, sizeof(num), "%d", idme.idme.bootmode);
	return simple_read_from_buffer(buf, size, ppos, num, strlen(num)); 
}

static const struct file_operations idme_bootmode_fops = {
	.read           = idme_bootmode_read,
};

static ssize_t idme_postmode_read(struct file *file, char __user *buf, size_t size, loff_t *ppos)
{
	char num[16] = {0};
	snprintf(num, sizeof(num), "%d", idme.idme.postmode);
	return simple_read_from_buffer(buf, size, ppos, num, strlen(num)); 
}
static const struct file_operations idme_postmode_fops = {
	.read           = idme_postmode_read,
};


static ssize_t idme_bootcount_read(struct file *file, char __user *buf, size_t size, loff_t *ppos)
{
	char num[16] = {0};
	snprintf(num, sizeof(num), "%d", idme.idme.boot_count);
	return simple_read_from_buffer(buf, size, ppos, num, strlen(num)); 
}
static const struct file_operations idme_bootcount_fops = {
	.read           = idme_bootcount_read,
};


static ssize_t idme_productid_read(struct file *file, char __user *buf, size_t size, loff_t *ppos)
{
	return simple_read_from_buffer(buf, size, ppos, idme.idme.productid, strlen(idme.idme.productid)); 
}
static const struct file_operations idme_productid_fops = {
	.read           = idme_productid_read,
};


static ssize_t idme_manufacture_read(struct file *file, char __user *buf, size_t size, loff_t *ppos)
{
	return simple_read_from_buffer(buf, size, ppos, idme.idme.manufacturing, strlen(idme.idme.manufacturing)); 
}
static const struct file_operations idme_manufacture_fops = {
	.read           = idme_manufacture_read,
};


static ssize_t idme_productid2_read(struct file *file, char __user *buf, size_t size, loff_t *ppos)
{
	return simple_read_from_buffer(buf, size, ppos, idme.idme.productid2, strlen(idme.idme.productid2)); 
}
static const struct file_operations idme_productid2_fops = {
	.read           = idme_productid2_read,
};

static ssize_t idme_unlock_code_read(struct file *file, char __user *buf, size_t size, loff_t *ppos)
{
	return simple_read_from_buffer(buf, size, ppos, idme.idme.unlock_code, strlen(idme.idme.unlock_code)); 
}
static const struct file_operations idme_unlock_code_fops = {
	.read           = idme_unlock_code_read,
};

static ssize_t idme_fos_flags_read(struct file *file, char __user *buf, size_t size, loff_t *ppos)
{
	char num[20] = {0};
    // fos_flags must be printed in hexadecimal. This is very important
    // for the correct interpretation of fos_flags from user space.
	snprintf(num, sizeof(num), "%llx", idme.idme.fos_flags);
	return simple_read_from_buffer(buf, size, ppos, num, strlen(num)); 
}
static const struct file_operations idme_fos_flags_fops = {
	.read           = idme_fos_flags_read,
};

static ssize_t idme_dev_flags_read(struct file *file, char __user *buf, size_t size, loff_t *ppos)
{
	char num[20] = {0};
	snprintf(num, sizeof(num), "%llx", idme.idme.dev_flags);
	return simple_read_from_buffer(buf, size, ppos, num, strlen(num)); 
}
static const struct file_operations idme_dev_flags_fops = {
	.read           = idme_dev_flags_read,
};

static ssize_t idme_usr_flags_read(struct file *file, char __user *buf, size_t size, loff_t *ppos)
{
	char num[20] = {0};
	snprintf(num, sizeof(num), "%llx", idme.idme.usr_flags);
	return simple_read_from_buffer(buf, size, ppos, num, strlen(num)); 
}
static const struct file_operations idme_usr_flags_fops = {
	.read           = idme_usr_flags_read,
};

static ssize_t idme_device_type_id_read(struct file *file, char __user *buf, size_t size, loff_t *ppos)
{
	return simple_read_from_buffer(buf, size, ppos, idme.idme.device_type_id, strlen(idme.idme.device_type_id));
}
static const struct file_operations idme_device_type_id_fops = {
	.read 		= idme_device_type_id_read,
};

static int __init idme_init(void)
{
	struct proc_dir_entry *proc_idme = NULL;
	struct proc_dir_entry *proc_serial, *proc_board_id, *proc_mac_addr, *proc_mac_sec, *proc_bt_mac_addr;
	struct proc_dir_entry *proc_bootmode, *proc_postmode, *proc_bootcount, *proc_productid;
	struct proc_dir_entry *proc_manufacture, *proc_productid2, *proc_unlockcode;
	struct proc_dir_entry *proc_fos_flags, *proc_dev_flags, *proc_usr_flags;

	proc_idme = proc_mkdir(IDME_DIRECTORY, NULL);
	if (!proc_idme) {
		return -ENOMEM;
	}

	proc_serial = proc_create(IDME_PROCNAME_SERIAL, S_IRUGO, proc_idme, &idme_serial_fops);
	proc_board_id = proc_create(IDME_PROCNAME_BOARDID, S_IRUGO, proc_idme, &idme_board_id_fops);
	proc_mac_addr = proc_create(IDME_PROCNAME_MACADDR, S_IRUGO, proc_idme, &idme_mac_address_fops);
	proc_mac_sec = proc_create(IDME_PROCNAME_MACSEC, S_IRUGO, proc_idme, &idme_mac_secret_fops);
	proc_bt_mac_addr = proc_create(IDME_PROCNAME_BTMACADDR, S_IRUGO, proc_idme, &idme_bt_mac_address_fops);
	proc_bootmode = proc_create(IDME_PROCNAME_BOOTMODE, S_IRUGO, proc_idme, &idme_bootmode_fops);
	proc_postmode = proc_create(IDME_PROCNAME_POSTMODE, S_IRUGO, proc_idme, &idme_postmode_fops);
	proc_bootcount = proc_create(IDME_PROCNAME_BOOTCOUNT, S_IRUGO, proc_idme, &idme_bootcount_fops);
	proc_productid = proc_create(IDME_PROCNAME_PRODUCTID, S_IRUGO, proc_idme, &idme_productid_fops);
	proc_manufacture = proc_create(IDME_PROCNAME_MANUFACTURING, S_IRUGO, proc_idme, &idme_manufacture_fops);
	proc_productid2 = proc_create(IDME_PROCNAME_PRODUCTID2, S_IRUGO, proc_idme, &idme_productid2_fops);
	proc_unlockcode = proc_create(IDME_PROCNAME_UNLOCK_CODE, S_IRUGO, proc_idme, &idme_unlock_code_fops);
	proc_fos_flags = proc_create(IDME_PROCNAME_FOS_FLAGS, S_IRUGO, proc_idme, &idme_fos_flags_fops);
	proc_dev_flags = proc_create(IDME_PROCNAME_DEV_FLAGS, S_IRUGO, proc_idme, &idme_dev_flags_fops);
	proc_usr_flags = proc_create(IDME_PROCNAME_USR_FLAGS, S_IRUGO, proc_idme, &idme_usr_flags_fops);
	proc_serial = proc_create(IDME_PROCNAME_DEVICETYPEID, S_IRUGO, proc_idme, &idme_device_type_id_fops);

	return 0;
}

static int __init init(void)
{
	int ret;

	ret = misc_register(&(idme.misc));
	if (unlikely(ret)) {
		return ret;
	}

	idme_init();
	return 0;
}


static int __init dummy_idme_init(void)
{
	int ret;
	ret = init();

	return ret;
}

device_initcall(dummy_idme_init);
