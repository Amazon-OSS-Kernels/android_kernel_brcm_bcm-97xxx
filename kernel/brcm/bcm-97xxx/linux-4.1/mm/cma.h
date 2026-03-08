#ifndef __MM_CMA_H__
#define __MM_CMA_H__

#ifdef CONFIG_CMA_BUFFER_LIST
struct cma_buffer {
	unsigned long pfn;
	unsigned long count;
	pid_t pid;
	char comm[TASK_COMM_LEN];
#ifdef CONFIG_CMA_ALLOC_STACKTRACE
	unsigned long trace_entries[16];
	unsigned int nr_entries;
#endif
	struct list_head list;
};

extern int cma_buffer_list_add(struct cma *cma, unsigned long pfn, int count);
extern void cma_buffer_list_del(struct cma *cma, unsigned long pfn, int count);
#else
#define cma_buffer_list_add(cma, pfn, count) { }
#define cma_buffer_list_del(cma, pfn, count) { }
#endif /* CONFIG_CMA_BUFFER_LIST */

#ifdef CONFIG_CMA_ALLOC_PROFILER
extern void cma_alloc_prof_start_ts(struct cma *cma, ktime_t *tstart);
extern void cma_alloc_prof_save_stats(struct cma *cma, ktime_t *tstart);
#else
#define cma_alloc_prof_start_ts(cma, tstart) { }
#define cma_alloc_prof_save_stats(cma, tstart) { }
#endif

#ifdef CONFIG_CMA_ALLOC_FREE_CALL_COUNTERS
extern void cma_alloc_call_counter_inc(struct cma *cma);
extern void cma_free_call_counter_inc(struct cma *cma);
#else
#define cma_alloc_call_counter_inc(cma) { }
#define cma_free_call_counter_inc(cma) { }
#endif

extern struct cma cma_areas[MAX_CMA_AREAS];
extern unsigned cma_area_count;

static inline unsigned long cma_bitmap_maxno(struct cma *cma)
{
	return cma->count >> cma->order_per_bit;
}

#endif
