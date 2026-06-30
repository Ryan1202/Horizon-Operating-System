/**
 * @file dma.h
 * @author Jiajun Wang (ryan1202@foxmail.com)
 * @brief 统一的DMA接口
 * @date 2025-03-12
 *
 * @copyright Copyright (c) 2025
 *
 */
#ifndef _DMA_H
#define _DMA_H

#include <kernel/driver.h>
#include <stdint.h>

typedef struct DmaConstraints {
	size_t	 mask;
	size_t	 coherent_mask;
	size_t	 boundary_mask;
	uint32_t max_segment_size;
	uint16_t max_segments;
} DmaConstraints;

#define DMA_BITS_PER_WORD (sizeof(size_t) * 8)
#define DMA_BIT_MASK(bit)                                                        \
	((size_t)(bit) >= DMA_BITS_PER_WORD                                      \
		 ? (size_t)~0                                                    \
		 : (((size_t)~0 >> ((DMA_BITS_PER_WORD - (size_t)(bit))          \
							& (DMA_BITS_PER_WORD - 1))) \
			& (0 - ((size_t)(bit) != 0))))

#define DMA_CONSTRAINTS_INIT(bit)                 \
	{.mask			   = DMA_BIT_MASK(bit),        \
	 .coherent_mask	   = DMA_BIT_MASK(bit),        \
	 .boundary_mask	   = 0,                       \
	 .max_segment_size = 0,                       \
	 .max_segments	   = 0}

typedef struct DmaDevice  DmaDevice;
typedef struct DmaHandle  DmaHandle;
typedef struct DmaBackend DmaBackend;

// Legacy ISA DMA callbacks. New bus-mastering drivers should use DmaDevice.
typedef struct Dma Dma;
typedef struct DmaOps {
	void *(*dma_alloc)(Dma *dma, uint32_t size);
	DriverResult (*dma_free)(Dma *dma, void *buffer, uint32_t size);
} DmaOps;

struct Dma {
	void   *dma;
	DmaOps *ops;
	void   *param;
};

extern const DmaBackend dma_backend_identity;

DmaDevice *dma_device_create(
	DmaConstraints constraints, const DmaBackend *backend, int coherent);
void dma_device_destroy(DmaDevice *device);

// 分配一块连续的、对齐到order的DMA内存，返回 DmaHandle 指针
DmaHandle *dma_alloc_coherent(DmaDevice *device, size_t size);
void	   dma_free_coherent(DmaHandle *handle);

// FFI 定义，
typedef enum DmaDirection {
	DmaNone			 = 0,
	DmaToDevice		 = 1,
	DmaFromDevice	 = 2,
	DmaBidirectional = 3,
} DmaDirection;

// ======== Stream API (使用 DmaHandle) ========

// 映射单个缓冲区，返回 DmaHandle 指针
DmaHandle *dma_map_single(
	DmaDevice *device, void *buffer, size_t length, DmaDirection direction);
// 解映射并释放 handle
void dma_unmap_single(DmaHandle *handle);

// 同步 handle 内 [offset, offset+size) 范围
int dma_handle_sync_for_device(
	const DmaHandle *handle, size_t offset, size_t size);
int dma_handle_sync_for_cpu(const DmaHandle *handle, size_t offset, size_t size);

// 访问器
void  *dma_handle_cpu_addr(const DmaHandle *handle);
size_t dma_handle_dma_addr(const DmaHandle *handle);
size_t dma_handle_size(const DmaHandle *handle);

// ======== SG API ========

typedef struct DmaScatterList {
	size_t entry_type; // private: frame/chain pointer plus internal flags
	size_t offset;	   // private: byte offset within the entry frame
	size_t length;	   // private: data length; use sg_dma_len()
	size_t dma_addr;   // result-only: use sg_dma_address() after dma_map_sg()
} DmaScatterList;

// DmaScatterList is the C view of Rust EntryList. Drivers must use helpers.
DmaScatterList *sg_create_segment(int capacity);
// Low-level compatibility initializer for caller-owned storage.
// New drivers should prefer sg_create_segment().
void sg_init_table(DmaScatterList *scatter_list, int n_entries);
void sg_set_buf(DmaScatterList *scatter_list, void *buffer, uint32_t length);

// 获取下一个entry
static inline DmaScatterList *sg_next(DmaScatterList *scatter_list) {
	enum {
		SG_CHAIN = 0x1,
		SG_END = 0x2,
		SG_MASK = 0x3,
	};

	if ((scatter_list->entry_type & SG_CHAIN) != 0) {
		return (DmaScatterList *)(scatter_list->entry_type & ~(size_t)SG_MASK);
	}
	if ((scatter_list->entry_type & SG_END) != 0 ||
		(scatter_list->entry_type & ~(size_t)SG_MASK) == 0) {
		return NULL;
	}
	return scatter_list + 1;
}
// 获取DMA地址（映射后使用）
static inline size_t sg_dma_address(DmaScatterList *scatter_list) {
	return scatter_list->dma_addr;
}
// 获取DMA长度（映射后使用）
static inline uint32_t sg_dma_len(DmaScatterList *scatter_list) {
	return (uint32_t)scatter_list->length;
}

// 映射scatterlist数组，返回成功映射的entry数量
int dma_map_sg(
	DmaDevice *device, DmaScatterList *scatter_list, int n_entries);
// 解映射
void dma_unmap_sg(DmaDevice *device, DmaScatterList *scatter_list, int n_entries);

// SG 同步（数据传输 + 内存屏障）
void dma_sync_sg_for_cpu(
	DmaDevice *device, DmaScatterList *scatter_list, int n_entries,
	DmaDirection direction);
void dma_sync_sg_for_device(
	DmaDevice *device, DmaScatterList *scatter_list, int n_entries,
	DmaDirection direction);

// ======== DmaPool API ========

typedef struct DmaPool DmaPool;

DmaPool *dma_pool_create(
	const char *name, DmaDevice *device, uint16_t size, size_t align);
void dma_pool_destroy(DmaPool *pool);
void *dma_pool_alloc(DmaPool *pool, size_t *dma_address);
void *dma_pool_zalloc(DmaPool *pool, size_t *dma_address);
void  dma_pool_free(DmaPool *pool, void *cpu_address);

// ======== Query API ========

// 返回设备单次 DMA 映射的最大字节数
size_t dma_max_mapping_size(const DmaDevice *device);
size_t dma_opt_mapping_size(const DmaDevice *device);
size_t dma_get_cache_alignment(void);
size_t dma_get_required_mask(const DmaDevice *device);
size_t dma_get_merge_boundary(const DmaDevice *device);
int	   dma_set_mask(DmaDevice *device, size_t mask);
int	   dma_set_coherent_mask(DmaDevice *device, size_t mask);
int	   dma_set_mask_and_coherent(DmaDevice *device, size_t mask);
int	   dma_set_max_seg_size(DmaDevice *device, uint32_t size);
uint32_t dma_get_max_seg_size(const DmaDevice *device);
int	   dma_set_seg_boundary(DmaDevice *device, size_t mask);
size_t dma_get_seg_boundary(const DmaDevice *device);

// 检查 DMA 映射是否失败（handle 为 NULL 表示失败）
static inline int dma_mapping_error(
	const DmaDevice *device, const DmaHandle *handle) {
	(void)device;
	return handle == NULL;
}

#endif
