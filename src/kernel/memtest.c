#include <driver/timer/timer_dm.h>
#include <kernel/device.h>
#include <kernel/memory.h>
#include <kernel/page.h>

extern TimerDeviceManager timer_dm_ext;

// 测试配置
#define TEST_ITERATIONS 1000	   // 每个测试案例的迭代次数
#define SMALL_SIZE		32		   // 小对象大小
#define MEDIUM_SIZE		1024	   // 中等对象大小
#define LARGE_SIZE		(4 * 1024) // 大对象大小（4KB）
#define HUGE_PAGE_CNT	4		   // 大页分配数量

// 性能统计结构体
struct perf_stats {
	uint32_t total_cycles;
	uint32_t max_cycles;
	uint32_t min_cycles;
	uint32_t alloc_fails;
};
// 内存块记录增强
struct mem_record {
	void  *ptr;
	size_t size;
	int	   is_page;
	int	   page_cnt; // 记录分配的页数
};

uint32_t rdtsc() {
	uint32_t lo, hi;
	__asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
	return lo;
}

uint32_t get_counter() {
	LogicalDevice *device = timer_dm_ext.scheduler_timer;
	TimerDevice	  *td	  = device->dm_ext;
	return td->counter;
}
// 初始化性能统计
static void init_stats(struct perf_stats *stats) {
	stats->total_cycles = 0;
	stats->max_cycles	= 0;
	stats->min_cycles	= __UINT32_MAX__;
	stats->alloc_fails	= 0;
}

// 更新统计信息
static void update_stats(struct perf_stats *stats, uint32_t cycles) {
	stats->total_cycles += cycles;
	if (cycles > stats->max_cycles) stats->max_cycles = cycles;
	if (cycles < stats->min_cycles) stats->min_cycles = cycles;
}

// 打印统计结果
static void print_stats(
	const char *test_name, const struct perf_stats *stats, int iter) {
	int avg_cycles = iter ? (stats->total_cycles / iter) : 0;
	printk("[%s] Results:\n", test_name);
	printk("  Total cycles: %u\n", stats->total_cycles);
	printk("  Avg cycles/op: %u\n", stats->total_cycles / iter);
	printk("  Max cycles: %u\n", stats->max_cycles);
	printk("  Min cycles: %u\n", stats->min_cycles);
	printk("  Allocation failures: %u\n", stats->alloc_fails);
}

void test_long_running(uint32_t minutes) {
	uint32_t start_count = get_counter();
	uint32_t end_count = start_count + (minutes * 60000); // 假设counter是毫秒级

	struct perf_stats stats;
	init_stats(&stats);
	uint32_t alloc_count = 0;
	Timer	 timer;
	timer_init(&timer);

	while (get_counter() < end_count) {
		size_t size = (alloc_count % 5 == 0) ? LARGE_SIZE : SMALL_SIZE;

		uint32_t start = rdtsc();
		void	*ptr   = kmalloc(size);
		uint32_t end   = rdtsc();

		if (!ptr) {
			stats.alloc_fails++;
			delay_ms(&timer, 10); // 背压延迟
			continue;
		}

		update_stats(&stats, end - start);
		alloc_count++;

		// 保持10个活跃对象
		static void *keep_alive[10];
		static int	 idx = 0;
		if (keep_alive[idx]) { kfree(keep_alive[idx]); }
		keep_alive[idx] = ptr;
		idx				= (idx + 1) % 10;
	}

	// printk("[Long Running] Ran for %u minutes\n", minutes);
	print_stats("Long Running", &stats, alloc_count);
}
void test_small_allocs(void) {
	struct perf_stats stats;
	init_stats(&stats);

	for (int i = 0; i < TEST_ITERATIONS; i++) {
		uint32_t start = rdtsc();
		void	*ptr   = kmalloc(SMALL_SIZE);
		uint32_t end   = rdtsc();

		if (!ptr) {
			stats.alloc_fails++;
			continue;
		}

		update_stats(&stats, end - start);
		kfree(ptr);
	}

	print_stats("Small Object Alloc", &stats, TEST_ITERATIONS);
}

void test_large_page_allocs(void) {
	struct perf_stats stats;
	init_stats(&stats);
	struct mem_record pages[HUGE_PAGE_CNT] = {0}; // 记录地址和页数

	for (int i = 0; i < TEST_ITERATIONS; i++) {
		uint32_t start = rdtsc();
		void	*ptr   = kmalloc_pages(HUGE_PAGE_CNT);
		uint32_t end   = rdtsc();

		if (!ptr) {
			stats.alloc_fails++;
			continue;
		}

		// 记录当前分配的页数
		pages[i % HUGE_PAGE_CNT].ptr	  = ptr;
		pages[i % HUGE_PAGE_CNT].page_cnt = HUGE_PAGE_CNT;
		update_stats(&stats, end - start);

		// 释放旧的页
		if (i >= HUGE_PAGE_CNT) {
			struct mem_record *old = &pages[i % HUGE_PAGE_CNT];
			if (old->ptr) {
				kfree_pages((int)old->ptr /*, old->page_cnt*/); // 关键修改点
				old->ptr = NULL;
			}
		}
	}

	// 清理残留页
	for (int i = 0; i < HUGE_PAGE_CNT; i++) {
		if (pages[i].ptr) {
			kfree_pages((int)pages[i].ptr /*, pages[i].page_cnt*/);
		}
	}

	print_stats("Large Page Alloc", &stats, TEST_ITERATIONS);
}

void run_memory_benchmarks(void) {
	printk("===== Starting Memory Allocator Benchmarks =====\n");

	Timer timer;
	timer_init(&timer);

	// 基础测试
	test_small_allocs();
	// delay_ms(&timer, 100);
	test_large_page_allocs();
	// delay_ms(&timer, 100);

	printk("===== Benchmark Suite Completed =====\n");
}