/*
 * CMA DebugFS Interface
 *
 * Copyright (c) 2015 Sasha Levin <sasha.levin@oracle.com>
 * Copyright (c) 2015 Stefan Strogin <stefan.strogin@gmail.com>
 */


#include <linux/debugfs.h>
#include <linux/list.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/mm_types.h>
#include <linux/mm.h>
#include <linux/stacktrace.h>
#include <linux/vmalloc.h>
#include <linux/pagemap.h>
#include <linux/cma.h>

#include "cma.h"

struct cma_mem {
	struct hlist_node node;
	struct page *p;
	unsigned long n;
};

static struct dentry *cma_debugfs_root;

#ifdef CONFIG_CMA_BUFFER_LIST
/* Must be called under cma->list_lock */
static int __cma_buffer_list_add(struct cma *cma, unsigned long pfn, int count)
{
	struct cma_buffer *cmabuf;
#ifdef CONFIG_CMA_ALLOC_STACKTRACE
	struct stack_trace trace;
#endif

	cmabuf = kmalloc(sizeof(*cmabuf), GFP_KERNEL);
	if (!cmabuf) {
		pr_warn("%s(page %p, count %d): failed to allocate buffer list entry\n",
			__func__, pfn_to_page(pfn), count);
		return -ENOMEM;
	}

#ifdef CONFIG_CMA_ALLOC_STACKTRACE
	trace.nr_entries = 0;
	trace.max_entries = ARRAY_SIZE(cmabuf->trace_entries);
	trace.entries = &cmabuf->trace_entries[0];
	trace.skip = 2;
	save_stack_trace(&trace);
	cmabuf->nr_entries = trace.nr_entries;
#endif
	cmabuf->pfn = pfn;
	cmabuf->count = count;
	cmabuf->pid = task_pid_nr(current);
	get_task_comm(cmabuf->comm, current);

	list_add_tail(&cmabuf->list, &cma->buffer_list);

	return 0;
}

/**
 * cma_buffer_list_add() - add a new entry to a list of allocated buffers
 * @cma:     Contiguous memory region for which the allocation is performed.
 * @pfn:     Base PFN of the allocated buffer.
 * @count:   Number of allocated pages.
 *
 * This function adds a new entry to the list of allocated contiguous memory
 * buffers in a CMA region.
 */
int cma_buffer_list_add(struct cma *cma, unsigned long pfn, int count)
{
	int ret;

	mutex_lock(&cma->list_lock);
	ret = __cma_buffer_list_add(cma, pfn, count);
	mutex_unlock(&cma->list_lock);

	return ret;
}

/**
 * cma_buffer_list_del() - delete an entry from a list of allocated buffers
 * @cma:   Contiguous memory region for which the allocation was performed.
 * @pfn:   Base PFN of the released buffer.
 * @count: Number of pages.
 *
 * This function deletes a list entry added by cma_buffer_list_add().
 */
void cma_buffer_list_del(struct cma *cma, unsigned long pfn, int count)
{
	struct cma_buffer *cmabuf, *tmp;
	int found = 0;
	unsigned long buf_end_pfn, free_end_pfn = pfn + count;

	mutex_lock(&cma->list_lock);
	list_for_each_entry_safe(cmabuf, tmp, &cma->buffer_list, list) {

		buf_end_pfn = cmabuf->pfn + cmabuf->count;
		if (pfn <= cmabuf->pfn && free_end_pfn >= buf_end_pfn) {
			list_del(&cmabuf->list);
			kfree(cmabuf);
			found = 1;
		} else if (pfn <= cmabuf->pfn &&
			   free_end_pfn >= cmabuf->pfn &&
			   free_end_pfn < buf_end_pfn) {
			cmabuf->count -= free_end_pfn - cmabuf->pfn;
			cmabuf->pfn = free_end_pfn;
			found = 1;
		} else if (pfn > cmabuf->pfn && pfn < buf_end_pfn) {
			if (free_end_pfn < buf_end_pfn)
				__cma_buffer_list_add(cma, free_end_pfn,
						buf_end_pfn - free_end_pfn);
			cmabuf->count = pfn - cmabuf->pfn;
			found = 1;
		}
	}
	mutex_unlock(&cma->list_lock);

	if (!found)
		pr_err("%s(page %p, count %d): couldn't find buffer list entry\n",
		       __func__, pfn_to_page(pfn), count);

}
#endif /* CONFIG_CMA_BUFFER_LIST */

#ifdef CONFIG_CMA_ALLOC_PROFILER
void cma_alloc_prof_start_ts(struct cma *cma, ktime_t *tstart)
{
	*tstart = ktime_get();
}

void cma_alloc_prof_save_stats(struct cma *cma, ktime_t *tstart)
{
	ktime_t tend, tdelta;
	static const ktime_t tzero = { .tv64 = 0 };

	if (ktime_equal(tzero, *tstart))
		return;

	tend = ktime_get();
	tdelta = ktime_sub(tend, *tstart);
	mutex_lock(&cma->lock);
	if ((ktime_equal(tzero,
		cma->prof.min_latency)) ||
	    (ktime_compare(tdelta,
		cma->prof.min_latency) < 0))
		cma->prof.min_latency = tdelta;

	if (ktime_compare(tdelta,
		cma->prof.max_latency) > 0)
		cma->prof.max_latency = tdelta;

	cma->prof.agg_latency = ktime_add(
		cma->prof.agg_latency, tdelta);
	cma->prof.sample_count++;
	mutex_unlock(&cma->lock);
}
#endif

#ifdef CONFIG_CMA_ALLOC_FREE_CALL_COUNTERS
void cma_alloc_call_counter_inc(struct cma *cma)
{
	atomic64_inc(&cma->alloc_call_cnt);
}

void cma_free_call_counter_inc(struct cma *cma)
{
	atomic64_inc(&cma->free_call_cnt);
}
#endif

static int cma_debugfs_get(void *data, u64 *val)
{
	unsigned long *p = data;

	*val = *p;

	return 0;
}
DEFINE_SIMPLE_ATTRIBUTE(cma_debugfs_fops, cma_debugfs_get, NULL, "%llu\n");

static int cma_used_get(void *data, u64 *val)
{
	struct cma *cma = data;
	unsigned long used;

	mutex_lock(&cma->lock);
	/* pages counter is smaller than sizeof(int) */
	used = bitmap_weight(cma->bitmap, (int)cma->count);
	mutex_unlock(&cma->lock);
	*val = (u64)used << cma->order_per_bit;

	return 0;
}
DEFINE_SIMPLE_ATTRIBUTE(cma_used_fops, cma_used_get, NULL, "%llu\n");

static int cma_maxchunk_get(void *data, u64 *val)
{
	struct cma *cma = data;
	unsigned long maxchunk = 0;
	unsigned long start, end = 0;

	mutex_lock(&cma->lock);
	for (;;) {
		start = find_next_zero_bit(cma->bitmap, cma->count, end);
		if (start >= cma->count)
			break;
		end = find_next_bit(cma->bitmap, cma->count, start);
		maxchunk = max(end - start, maxchunk);
	}
	mutex_unlock(&cma->lock);
	*val = (u64)maxchunk << cma->order_per_bit;

	return 0;
}
DEFINE_SIMPLE_ATTRIBUTE(cma_maxchunk_fops, cma_maxchunk_get, NULL, "%llu\n");

static void cma_add_to_cma_mem_list(struct cma *cma, struct cma_mem *mem)
{
	spin_lock(&cma->mem_head_lock);
	hlist_add_head(&mem->node, &cma->mem_head);
	spin_unlock(&cma->mem_head_lock);
}

static struct cma_mem *cma_get_entry_from_list(struct cma *cma)
{
	struct cma_mem *mem = NULL;

	spin_lock(&cma->mem_head_lock);
	if (!hlist_empty(&cma->mem_head)) {
		mem = hlist_entry(cma->mem_head.first, struct cma_mem, node);
		hlist_del_init(&mem->node);
	}
	spin_unlock(&cma->mem_head_lock);

	return mem;
}

static int cma_free_mem(struct cma *cma, int count)
{
	struct cma_mem *mem = NULL;

	while (count) {
		mem = cma_get_entry_from_list(cma);
		if (mem == NULL)
			return 0;

		if (mem->n <= count) {
			cma_release(cma, mem->p, mem->n);
			count -= mem->n;
			kfree(mem);
		} else if (cma->order_per_bit == 0) {
			cma_release(cma, mem->p, count);
			mem->p += count;
			mem->n -= count;
			count = 0;
			cma_add_to_cma_mem_list(cma, mem);
		} else {
			pr_debug("cma: cannot release partial block when order_per_bit != 0\n");
			cma_add_to_cma_mem_list(cma, mem);
			break;
		}
	}

	return 0;

}

static int cma_free_write(void *data, u64 val)
{
	int pages = val;
	struct cma *cma = data;

	return cma_free_mem(cma, pages);
}
DEFINE_SIMPLE_ATTRIBUTE(cma_free_fops, NULL, cma_free_write, "%llu\n");

static int cma_alloc_mem(struct cma *cma, int count)
{
	struct cma_mem *mem;
	struct page *p;

	mem = kzalloc(sizeof(*mem), GFP_KERNEL);
	if (!mem)
		return -ENOMEM;

	p = cma_alloc(cma, count, 0);
	if (!p) {
		kfree(mem);
		return -ENOMEM;
	}

	mem->p = p;
	mem->n = count;

	cma_add_to_cma_mem_list(cma, mem);

	return 0;
}

static int cma_alloc_write(void *data, u64 val)
{
	int pages = val;
	struct cma *cma = data;

	return cma_alloc_mem(cma, pages);
}
DEFINE_SIMPLE_ATTRIBUTE(cma_alloc_fops, NULL, cma_alloc_write, "%llu\n");

#ifdef CONFIG_CMA_BUFFER_LIST
static int cma_buffer_list_show(struct seq_file *s, void *data)
{
	struct cma *cma = s->private;
	struct cma_buffer *cmabuf;
#ifdef CONFIG_CMA_ALLOC_STACKTRACE
	char *buf;
	struct stack_trace trace;
#endif
	if (!cma)
		return -EINVAL;

	mutex_lock(&cma->list_lock);
	list_for_each_entry(cmabuf, &cma->buffer_list, list) {
		seq_printf(s, "0x%llx - 0x%llx (%lu kB), allocated by pid %u (%s)\n",
			(unsigned long long)PFN_PHYS(cmabuf->pfn),
			(unsigned long long)PFN_PHYS(cmabuf->pfn +
				cmabuf->count),
			(cmabuf->count * PAGE_SIZE) >> 10, cmabuf->pid,
			cmabuf->comm);

#ifdef CONFIG_CMA_ALLOC_STACKTRACE
		buf = kzalloc(PAGE_SIZE, GFP_KERNEL);
		if (!buf) {
			pr_err("%s: failed to allocate buffer for stackframe\n",
				__func__);
			goto out;
		}
		trace.nr_entries = cmabuf->nr_entries;
		trace.entries = &cmabuf->trace_entries[0];
		snprint_stack_trace(buf, PAGE_SIZE, &trace, 0);
		seq_printf(s, "%s\n", buf);
		kfree(buf);
#endif
	}
out:
	mutex_unlock(&cma->list_lock);

	return 0;
}

static int cma_buffer_list_open(struct inode *inode, struct file *file)
{
	return single_open(file, cma_buffer_list_show, inode->i_private);
}

static const struct file_operations cma_buffer_list_fops = {
	.open = cma_buffer_list_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};
#endif /* CONFIG_CMA_BUFFER_LIST */

static int cma_free_page_state_show(struct seq_file *s, void *data)
{
	struct cma *cma = s->private;
	unsigned long bitmap_no, bitmap_maxno, prev_pfn = 0, pfn;
	struct page *page = NULL;
	int i, pin_count;
	bool locked, dirty, writeback;

	seq_puts(s, "\nFree block page state:\n");
	seq_puts(s, "Format: freeblock_start_pfn/physaddr: L[W][D]|F,...\n");
	seq_puts(s, "L => Locked, W => Writeback, D => Dirty, F => Free\n");

	bitmap_maxno = cma_bitmap_maxno(cma);
	mutex_lock(&cma->lock);

	for_each_clear_bit(bitmap_no, cma->bitmap, bitmap_maxno) {
		for (i = 0; i < (1 << cma->order_per_bit); i++) {
			pfn = cma->base_pfn + i +
			      (bitmap_no << cma->order_per_bit);
			page = pfn_to_page(pfn);

			lock_page(page);
			if (PageBuddy(page)) {
				locked = dirty = writeback = false;
				goto skip;
			}
			pin_count = page_count(page) - page_mapcount(page);
			if (page_mapping(page))
				pin_count -= 1 + page_has_private(page);
			locked = (pin_count > 0) ? true : false;
			dirty = PageDirty(page) ? true : false;
			writeback = PageWriteback(page) ? true : false;
skip:
			unlock_page(page);

			if (prev_pfn == (pfn - 1))
				seq_puts(s, ", ");
			else {
				phys_addr_t paddr = page_to_phys(page);
				seq_printf(s, "\n\n%lu/%pa: ",
					pfn, &paddr);
			}
			if (locked)
				seq_puts(s, "L");
			if (dirty)
				seq_puts(s, "D");
			if (writeback)
				seq_puts(s, "W");
			if (!dirty && !locked && !writeback)
				seq_puts(s, "F");
			prev_pfn = pfn;
		}
	}
	seq_puts(s, "\n");

	mutex_unlock(&cma->lock);

	return 0;
}

static int cma_free_page_state_open(struct inode *inode, struct file *file)
{
	return single_open(file, cma_free_page_state_show, inode->i_private);
}

static const struct file_operations cma_free_page_state_fops = {
	.open = cma_free_page_state_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

#ifdef CONFIG_CMA_ALLOC_PROFILER
static ssize_t cma_alloc_latency_read(struct file *file, char __user *userbuf,
		size_t count, loff_t *ppos)
{
	struct cma *cma = file->private_data;
	u64 min, max, avg;
	char buf[100];
	u32 sample_count;
	ssize_t len;

	if (!cma)
		return -EINVAL;

	mutex_lock(&cma->lock);
	min = (u64)ktime_to_ns(cma->prof.min_latency);
	max = (u64)ktime_to_ns(cma->prof.max_latency);
	avg = (u64)ktime_to_ns(cma->prof.agg_latency);
	sample_count = cma->prof.sample_count;
	mutex_unlock(&cma->lock);

	if (sample_count)
		do_div(avg, sample_count);
	else
		avg = 0;
	len = snprintf(buf, sizeof(buf),
		"min:%llu ns, max:%llu ns, avg:%llu ns\n",
		min, max, avg);

	return simple_read_from_buffer(userbuf, count, ppos, buf, len);
}

static ssize_t cma_alloc_latency_clear(struct file *file,
		const char __user *userbuf, size_t count, loff_t *ppos)
{
	struct cma *cma = file->private_data;

	if (!cma)
		return -EINVAL;

	mutex_lock(&cma->lock);
	memset(&cma->prof, 0, sizeof(cma->prof));
	mutex_unlock(&cma->lock);

	return count;
}

static const struct file_operations cma_alloc_latency_fops = {
	.open = simple_open,
	.read = cma_alloc_latency_read,
	.write = cma_alloc_latency_clear,
};
#endif

#ifdef CONFIG_CMA_ALLOC_FREE_CALL_COUNTERS
static int cma_alloc_call_cnt_get(void *data, u64 *val)
{
	struct cma *cma = data;

	if (!cma)
		return -EINVAL;

	*val = atomic64_read(&cma->alloc_call_cnt);

	return 0;
}

static int cma_alloc_call_cnt_clear(void *data, u64 val)
{
	struct cma *cma = data;

	if (!cma)
		return -EINVAL;

	atomic64_set(&cma->alloc_call_cnt, 0);

	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(cma_alloc_call_cnt_fops, cma_alloc_call_cnt_get,
			cma_alloc_call_cnt_clear, "%llu\n");

static int cma_free_call_cnt_get(void *data, u64 *val)
{
	struct cma *cma = data;

	if (!cma)
		return -EINVAL;

	*val = atomic64_read(&cma->free_call_cnt);

	return 0;
}

static int cma_free_call_cnt_clear(void *data, u64 val)
{
	struct cma *cma = data;

	if (!cma)
		return -EINVAL;

	atomic64_set(&cma->free_call_cnt, 0);

	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(cma_free_call_cnt_fops, cma_free_call_cnt_get,
			cma_free_call_cnt_clear, "%llu\n");
#endif


static void cma_debugfs_add_one(struct cma *cma, int idx)
{
	struct dentry *tmp;
	char name[16];
	int u32s;

	sprintf(name, "cma-%d", idx);

	tmp = debugfs_create_dir(name, cma_debugfs_root);

	debugfs_create_file("alloc", S_IWUSR, cma_debugfs_root, cma,
				&cma_alloc_fops);

	debugfs_create_file("free", S_IWUSR, cma_debugfs_root, cma,
				&cma_free_fops);

	debugfs_create_file("base_pfn", S_IRUGO, tmp,
				&cma->base_pfn, &cma_debugfs_fops);
	debugfs_create_file("count", S_IRUGO, tmp,
				&cma->count, &cma_debugfs_fops);
	debugfs_create_file("order_per_bit", S_IRUGO, tmp,
				&cma->order_per_bit, &cma_debugfs_fops);
	debugfs_create_file("used", S_IRUGO, tmp, cma, &cma_used_fops);
	debugfs_create_file("maxchunk", S_IRUGO, tmp, cma, &cma_maxchunk_fops);
#ifdef CONFIG_CMA_BUFFER_LIST
	debugfs_create_file("buffers", S_IRUGO, tmp, cma,
				&cma_buffer_list_fops);
#endif

	u32s = DIV_ROUND_UP(cma_bitmap_maxno(cma), BITS_PER_BYTE * sizeof(u32));
	debugfs_create_u32_array("bitmap", S_IRUGO, tmp, (u32*)cma->bitmap, u32s);

	debugfs_create_file("free_page_state", S_IRUGO, tmp, cma,
				&cma_free_page_state_fops);
#ifdef CONFIG_CMA_ALLOC_PROFILER
	debugfs_create_file("alloc_latency", (S_IRUGO | S_IWUSR), tmp, cma,
				&cma_alloc_latency_fops);
#endif
#ifdef CONFIG_CMA_ALLOC_FREE_CALL_COUNTERS
	debugfs_create_file("alloc_call_cnt", (S_IRUGO | S_IWUSR), tmp, cma,
				&cma_alloc_call_cnt_fops);
	debugfs_create_file("free_call_cnt", (S_IRUGO | S_IWUSR), tmp, cma,
				&cma_free_call_cnt_fops);
#endif
}

static int __init cma_debugfs_init(void)
{
	int i;

	cma_debugfs_root = debugfs_create_dir("cma", NULL);
	if (!cma_debugfs_root)
		return -ENOMEM;

	for (i = 0; i < cma_area_count; i++)
		cma_debugfs_add_one(&cma_areas[i], i);

	return 0;
}
late_initcall(cma_debugfs_init);
