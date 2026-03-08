#ifndef __CMA_H__
#define __CMA_H__

/*
 * There is always at least global CMA area and a few optional
 * areas configured in kernel .config.
 */
#ifdef CONFIG_CMA_AREAS
#define MAX_CMA_AREAS	(1 + CONFIG_CMA_AREAS)

#else
#define MAX_CMA_AREAS	(0)

#endif

#ifdef CONFIG_CMA_ALLOC_PROFILER
struct cma_alloc_prof {
	ktime_t min_latency;
	ktime_t max_latency;
	ktime_t agg_latency;
	u32 sample_count;
};
#endif

struct cma {
	unsigned long	base_pfn;
	unsigned long	count;
	unsigned long	*bitmap;
	unsigned int order_per_bit; /* Order of pages represented by one bit */
	struct mutex	lock;
#ifdef CONFIG_CMA_DEBUGFS
	struct hlist_head mem_head;
	spinlock_t mem_head_lock;
#endif
#ifdef CONFIG_CMA_BUFFER_LIST
	struct list_head buffer_list;
	struct mutex	list_lock;
#endif
#ifdef CONFIG_CMA_ALLOC_PROFILER
	struct cma_alloc_prof prof;
#endif
#ifdef CONFIG_CMA_ALLOC_FREE_CALL_COUNTERS
	atomic64_t alloc_call_cnt;
	atomic64_t free_call_cnt;
#endif
};

extern unsigned long totalcma_pages;
extern phys_addr_t cma_get_base(const struct cma *cma);
extern unsigned long cma_get_size(const struct cma *cma);

extern int __init cma_declare_contiguous(phys_addr_t base,
			phys_addr_t size, phys_addr_t limit,
			phys_addr_t alignment, unsigned int order_per_bit,
			bool fixed, struct cma **res_cma);
extern int cma_init_reserved_mem(phys_addr_t base, phys_addr_t size,
					unsigned int order_per_bit,
					struct cma **res_cma);
extern struct page *cma_alloc(struct cma *cma, unsigned int count, unsigned int align);
extern bool cma_release(struct cma *cma, const struct page *pages, unsigned int count);
#endif
