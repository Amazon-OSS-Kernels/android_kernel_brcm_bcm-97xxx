/*
 * idme.c
 *
 * Copyright 2013 Amazon Technologies, Inc. All Rights Reserved.
 *
 * The code contained herein is licensed under the GNU General Public
 * License Version 2. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

#include <linux/kernel.h>

#include <linux/init.h>

#define IDME_OF_BOARD_ID	"/idme/board_id"
#define IDME_OF_BOOT_MODE	"/idme/bootmode"
#define IDME_OF_DEV_FLAGS       "/idme/dev_flags"
#define IDME_OF_USR_FLAGS       "/idme/usr_flags"
#define IDME_OF_PRODUCTID2       "/idme/productid2"
#define IDME_OF_FOS_FLAGS        "/idme/fos_flags"

#define MAC_SEC_KEY "mac_sec"
#define MAC_SEC_OWNER 1000

const char *product_id2 = NULL;
EXPORT_SYMBOL(product_id2);

static int idme_proc_show(struct seq_file *seq, void *v)
{
	struct property *pp = (struct property *)seq->private;

	BUG_ON(!pp);

	seq_write(seq, pp->value, pp->length);

	return 0;
}

static int idme_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, idme_proc_show, PDE_DATA(inode));
}

static const struct file_operations idme_fops = {
	.owner = THIS_MODULE,
	.open = idme_proc_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

bool board_has_wan(void)
{
	struct device_node *ap;
	int len;

	ap = of_find_node_by_path(IDME_OF_BOARD_ID);
	if (ap) {
		const char *boardid = of_get_property(ap, "value", &len);
		if (len >= 2) {
			if (boardid[0] == '0' && boardid[5] == '1')
				return true;
		}
	}

	return false;
}

EXPORT_SYMBOL(board_has_wan);

unsigned int idme_get_board_type(void)
{
	struct device_node *ap = NULL;
	char board_type[5] = { 0 };
	const char *board_id = NULL;
	unsigned int rs = 0;

	ap = of_find_node_by_path(IDME_OF_BOARD_ID);
	if (ap)
		board_id = (const char *)of_get_property(ap, "value", NULL);
	else
		pr_err("of_find_node_by_path failed\n");

	strlcpy(board_type, board_id, sizeof(board_type));
	if (unlikely(kstrtouint(board_type, 16, &rs)))
		pr_err("idme_get_board_type kstrtouint failed!\v");

	return rs;
}

EXPORT_SYMBOL(idme_get_board_type);

unsigned int idme_get_board_rev(void)
{
	struct device_node *ap = NULL;
	char board_rev[3] = { 0 };
	const char *board_id = NULL;
	unsigned int rs = 0;

	ap = of_find_node_by_path(IDME_OF_BOARD_ID);
	if (ap)
		board_id = (const char *)of_get_property(ap, "value", NULL);
	else
		pr_err("of_find_node_by_path failed\n");

	strlcpy(board_rev, (board_id + 7), sizeof(board_rev));

	if (unlikely(kstrtouint(board_rev, 16, &rs)))
		pr_err("idme_get_board_rev kstrtouint failed!\v");

	return rs;
}

EXPORT_SYMBOL(idme_get_board_rev);

unsigned int idme_get_bootmode(void)
{
	struct device_node *idme_node;
	int len;

	idme_node = of_find_node_by_path(IDME_OF_BOOT_MODE);
	if (idme_node) {
		const char *bootmode = of_get_property(idme_node, "value", &len);
		if (len > 0) {
			return bootmode[0] - '0';
		}
	}

	return 0;
}
EXPORT_SYMBOL(idme_get_bootmode);

u64 idme_get_dev_flags_value(void)
{
	struct device_node *ap = NULL;
	char *devflags = NULL;
	int res;
	u64 ret;

	ap = of_find_node_by_path(IDME_OF_DEV_FLAGS);
	if (ap) {
		devflags = (char *)of_get_property(ap, "value", NULL);
		pr_info("dev_flags %s\n", devflags);
	} else {
		pr_err("of_find_node_by_path failed\n");
	}
	res = kstrtoull(devflags, 16, &ret);
	if (res)
		ret = 0;
	pr_info("dev_flags: %llu\n", ret);
	return ret;
}
EXPORT_SYMBOL(idme_get_dev_flags_value);

u64 idme_get_usr_flags_value(void)
{
	struct device_node *ap = NULL;
	char *usrflags = NULL;
	int res;
	u64 ret;

	ap = of_find_node_by_path(IDME_OF_USR_FLAGS);
	if (ap) {
		usrflags = (char *)of_get_property(ap, "value", NULL);
		pr_info("usr_flags %s\n", usrflags);
	} else {
		pr_err("of_find_node_by_path failed\n");
	}
	res = kstrtoull(usrflags, 16, &ret);
	if (res)
		ret = 0;
	pr_info("usr_flags: %llu\n", ret);
	return ret;
}
EXPORT_SYMBOL(idme_get_usr_flags_value);

char *idme_get_productid2_value(void){
	struct device_node *ap = NULL;
	const char *pid2 = NULL;

	ap = of_find_node_by_path(IDME_OF_PRODUCTID2);
	if (ap)
		pid2 = (const char *)of_get_property(ap, "value", NULL);
	else
		pr_err("of_find_node_by_path failed\n");

	return pid2;
}
EXPORT_SYMBOL(idme_get_productid2_value);

u64 idme_get_fos_flags_value(void)
{
	struct device_node *ap = NULL;
	char *fosflags = NULL;
	int res;
	u64 ret;

	ap = of_find_node_by_path(IDME_OF_FOS_FLAGS);
	if (ap)
		fosflags = (char *)of_get_property(ap, "value", NULL);
	res = kstrtoull(fosflags, 16, &ret);
	if (res)
		ret = 0;
	return ret;
}
EXPORT_SYMBOL(idme_get_fos_flags_value);

static int __init idme_init(void)
{
	struct proc_dir_entry *proc_idme = NULL;
	struct device_node *root = NULL, *child = NULL;
	struct property *pp_value = NULL;
	int perm = 0;
	struct proc_dir_entry *child_pde = NULL;
	bool access_restrict = false;

	root = of_find_node_by_path("/idme");
	if (!root)
		return -EINVAL;

	/* Create the root IDME procfs node */
	proc_idme = proc_mkdir("idme", NULL);
	if (!proc_idme) {
		of_node_put(root);
		return -ENOMEM;
	}

	/* Populate each IDME field */
	for (child = NULL; (child = of_get_next_child(root, child)); ) {
		pp_value = of_find_property(child, "value", NULL);

		if (strcmp(child->name, MAC_SEC_KEY) == 0)
			access_restrict = true;
		else
			access_restrict = false;

		if (!pp_value)
			continue;

		if (of_property_read_u32(child, "permission", &perm))
			continue;

		/* These values aren't writable anyways */
		perm &= ~(S_IWUGO);

		if (access_restrict)
			perm = 0400;

		child_pde = proc_create_data(child->name, perm, proc_idme,
			&idme_fops, pp_value);

		if (child_pde && access_restrict)
			proc_set_user(child_pde, KUIDT_INIT(MAC_SEC_OWNER), KGIDT_INIT(0));
	}

	of_node_put(child);
	of_node_put(root);

	product_id2 = idme_get_productid2_value();
	return 0;
}

fs_initcall(idme_init);
