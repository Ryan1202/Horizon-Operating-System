use core::{
    ffi::{CStr, c_char, c_int, c_void},
    mem::size_of,
    num::{NonZeroU16, NonZeroUsize},
    ptr::{NonNull, drop_in_place, null_mut},
    sync::atomic::Ordering,
};

use crate::{CACHELINE_SIZE, arch::ArchPageTable};
use crate::{
    impl_addr,
    kernel::memory::{
        arch::ArchMemory,
        dma::{
            constraints::Constraints, device::Device, handle::DmaHandle, mapping::Backend,
            scatter_gather::EntryList as DmaScatterList, source::pool::Pool,
        },
        frame::TOTAL_PAGES,
        kmalloc::{kfree, kmalloc},
    },
};

pub mod constraints;
pub mod device;
pub mod handle;
pub mod mapping;
pub mod scatter_gather;
pub mod source;
pub mod sync;

/// DMA 设备地址，布局与 usize 相同
#[repr(transparent)]
#[derive(Clone, Copy, Debug)]
pub struct DmaAddr(usize);

impl DmaAddr {
    pub const fn new(addr: usize) -> Self {
        Self(addr)
    }

    pub const fn as_usize(self) -> usize {
        self.0
    }
}

impl_addr!(DmaAddr);

/// DMA 映射方向
///
/// 取值与 C 的 DmaDirection 兼容：
///   None = 0
///   ToDevice = 1
///   FromDevice = 2
///   Bidirectional = 3
#[repr(u8)]
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Direction {
    None = 0,
    ToDevice = 1,
    FromDevice = 2,
    Bidirectional = 3,
}

impl Direction {
    fn from_int(raw: c_int) -> Option<Self> {
        match raw {
            0 => Some(Self::None),
            1 => Some(Self::ToDevice),
            2 => Some(Self::FromDevice),
            3 => Some(Self::Bidirectional),
            _ => None,
        }
    }
}

#[unsafe(export_name = "dma_device_create")]
pub extern "C" fn device_create_c(
    constraints: Constraints,
    backend: *const Backend,
    coherent: c_int,
) -> *mut Device {
    let backend = if let Some(backend) = unsafe { backend.as_ref() } {
        backend
    } else {
        return null_mut();
    };

    let device = Device::new(constraints, *backend, coherent != 0);

    const DMA_DEVICE_SIZE: NonZeroUsize = NonZeroUsize::new(size_of::<Device>()).unwrap();
    match kmalloc::<Device>(DMA_DEVICE_SIZE) {
        Some(ptr) => {
            unsafe { ptr.as_ptr().write(device) };
            ptr.as_ptr()
        }
        None => {
            printk!("WARNING: dma_device_create failed: out of memory\n");
            null_mut()
        }
    }
}

#[unsafe(export_name = "dma_device_destroy")]
pub extern "C" fn device_destroy_c(device: *mut Device) {
    if let Some(device) = NonNull::new(device) {
        unsafe { drop_in_place(device.as_ptr()) };

        if let Err(e) = kfree(device) {
            printk!("WARNING: dma_device_destroy failed: {:?}\n", e);
        }
    }
}

#[unsafe(export_name = "dma_alloc_coherent")]
pub extern "C" fn alloc_coherent_c(device: *mut Device, size: usize) -> *mut DmaHandle {
    let Some(device) = (unsafe { device.as_mut() }) else {
        return null_mut();
    };

    match device.alloc_coherent(size) {
        Ok(handle) => {
            // 堆分配 handle 并返回指针，C 侧负责管理生命周期
            const HANDLE_SIZE: NonZeroUsize = NonZeroUsize::new(size_of::<DmaHandle>()).unwrap();
            match kmalloc::<DmaHandle>(HANDLE_SIZE) {
                Some(ptr) => {
                    unsafe { ptr.as_ptr().write(handle) };
                    ptr.as_ptr()
                }
                None => {
                    printk!("WARNING: dma_alloc_coherent failed: out of memory for handle\n");

                    null_mut()
                }
            }
        }
        Err(e) => {
            printk!(
                "WARNING: dma_alloc_coherent failed: size = {:#x}, error = {:?}\n",
                size,
                e
            );
            null_mut()
        }
    }
}

#[unsafe(export_name = "dma_free_coherent")]
pub extern "C" fn free_coherent_c(handle: *mut DmaHandle) {
    let Some(handle_ptr) = NonNull::new(handle) else {
        return;
    };

    unsafe { drop_in_place(handle_ptr.as_ptr()) };

    if let Err(e) = kfree(handle_ptr) {
        printk!(
            "WARNING: dma_free_coherent failed to free handle memory: {:?}\n",
            e
        );
    }
}

#[unsafe(export_name = "dma_pool_create")]
pub extern "C" fn pool_create_c(
    name: *const c_char,
    device: *mut Device,
    size: u16,
    align: usize,
) -> *mut Pool {
    if name.is_null() {
        return null_mut();
    }

    let Some((device, object_size)) = unsafe { device.as_mut() }.zip(NonZeroU16::new(size)) else {
        return null_mut();
    };

    let name = unsafe { CStr::from_ptr(name) };
    device
        .create_pool(name, object_size, align)
        .map_or(null_mut(), NonNull::as_ptr)
}

#[unsafe(export_name = "dma_pool_destroy")]
pub extern "C" fn pool_destroy_c(pool: *mut Pool) {
    if let Some(pool) = NonNull::new(pool) {
        if let Err(e) = Pool::destroy(pool) {
            printk!("WARNING: dma_pool_destroy failed: {:?}\n", e);
        }
    }
}

#[unsafe(export_name = "dma_pool_alloc")]
pub extern "C" fn pool_alloc_c(pool: *mut Pool, out_dma: *mut usize) -> *mut Pool {
    pool_alloc(pool, out_dma, false)
}

#[unsafe(export_name = "dma_pool_zalloc")]
pub extern "C" fn pool_zalloc_c(pool: *mut Pool, out_dma: *mut usize) -> *mut Pool {
    pool_alloc(pool, out_dma, true)
}

fn pool_alloc(pool: *mut Pool, out_dma: *mut usize, zeroed: bool) -> *mut Pool {
    if out_dma.is_null() {
        return null_mut();
    }
    unsafe {
        *out_dma = 0;
    }
    let Some(pool) = (unsafe { pool.as_ref() }) else {
        return null_mut();
    };
    let Ok((vaddr, dma_addr)) = pool.allocate() else {
        printk!("WARNING: dma_pool_alloc failed\n");
        return null_mut();
    };
    if zeroed {
        unsafe { core::ptr::write_bytes(vaddr.as_mut_ptr::<u8>(), 0, pool.object_size()) };
    }
    unsafe {
        *out_dma = dma_addr.as_usize();
    }
    vaddr.as_mut_ptr()
}

#[unsafe(export_name = "dma_pool_free")]
pub extern "C" fn pool_free_c(pool: *mut Pool, ptr: *mut c_void) {
    let Some(pool) = (unsafe { pool.as_ref() }) else {
        return;
    };
    let Some(ptr) = NonNull::new(ptr) else {
        return;
    };
    if let Err(e) = pool.deallocate(ptr) {
        printk!("WARNING: dma_pool_free failed: {:?}\n", e);
    }
}

fn checked_entries_count(n_entries: c_int) -> Option<usize> {
    if n_entries <= 0 {
        None
    } else {
        Some(n_entries as usize)
    }
}

/// 创建一个 scatter list segment，返回指向 DmaScatterList 的指针
#[unsafe(export_name = "sg_create_segment")]
pub extern "C" fn sg_create_segment_c(capacity: c_int) -> *mut DmaScatterList {
    let Some(entries_count) = checked_entries_count(capacity) else {
        return null_mut();
    };
    DmaScatterList::create_segment(entries_count).map_or(null_mut(), NonNull::as_ptr)
}

/// 在已分配的内存上初始化 scatter list，不建议使用
#[unsafe(export_name = "sg_init_table")]
pub extern "C" fn sg_init_table_c(scatter_list: *mut DmaScatterList, n_entries: c_int) {
    let Some(scatter_list) = (unsafe { scatter_list.as_mut() }) else {
        return;
    };
    let Some(entries_count) = checked_entries_count(n_entries) else {
        return;
    };

    unsafe { scatter_list.init_array(entries_count) };
}

#[unsafe(export_name = "sg_set_buf")]
pub extern "C" fn sg_set_buf_c(
    scatter_list: *mut DmaScatterList,
    buffer: *mut c_void,
    length: u32,
) {
    if scatter_list.is_null() || buffer.is_null() {
        return;
    }
    let Some(entry) = (unsafe { scatter_list.as_mut() }) else {
        return;
    };
    entry.set_buffer(buffer.cast(), length as usize);
}

#[unsafe(export_name = "dma_map_sg")]
pub extern "C" fn map_sg_c(
    device: *mut Device,
    scatter_list: *mut DmaScatterList,
    n_entries: c_int,
) -> i32 {
    let Some((device, entries_count)) =
        unsafe { device.as_mut() }.zip(checked_entries_count(n_entries))
    else {
        return -1;
    };

    let Some(scatter_list) = (unsafe { scatter_list.as_mut() }) else {
        return -1;
    };

    match device.map_sg(scatter_list, entries_count) {
        Ok(mapped) => mapped as i32,
        Err(e) => {
            printk!("WARNING: dma_map_sg failed: {:?}\n", e);
            -1
        }
    }
}

#[unsafe(export_name = "dma_unmap_sg")]
pub extern "C" fn unmap_sg_c(
    device: *mut Device,
    scatter_list: *mut DmaScatterList,
    n_entries: c_int,
) {
    let Some((device, entries_count)) =
        unsafe { device.as_mut() }.zip(checked_entries_count(n_entries))
    else {
        return;
    };
    let Some(scatter_list) = (unsafe { scatter_list.as_mut() }) else {
        return;
    };

    if let Err(e) = device.unmap_sg(scatter_list, entries_count) {
        printk!("WARNING: dma_unmap_sg failed: {:?}\n", e);
    }
}

#[unsafe(export_name = "dma_map_single")]
pub extern "C" fn map_single_c(
    device: *mut Device,
    ptr: *mut c_void,
    size: usize,
    direction: c_int,
) -> *mut DmaHandle {
    let Some((device, ptr)) = (unsafe { device.as_mut().zip(NonNull::new(ptr)) }) else {
        return null_mut();
    };

    let Some(direction) = Direction::from_int(direction) else {
        printk!("WARNING: dma_map_single failed: invalid direction\n");
        return null_mut();
    };

    match device.map_single(ptr, size, direction) {
        Ok(handle) => {
            const HANDLE_SIZE: NonZeroUsize = NonZeroUsize::new(size_of::<DmaHandle>()).unwrap();
            match kmalloc::<DmaHandle>(HANDLE_SIZE) {
                Some(ptr) => {
                    unsafe { ptr.as_ptr().write(handle) };
                    ptr.as_ptr()
                }
                None => {
                    printk!("WARNING: dma_map_single failed: out of memory for handle\n");
                    null_mut()
                }
            }
        }
        Err(e) => {
            printk!(
                "WARNING: dma_map_single failed: size = {:#x}, error = {:?}\n",
                size,
                e
            );
            null_mut()
        }
    }
}

#[unsafe(export_name = "dma_unmap_single")]
pub extern "C" fn unmap_single_c(handle: *mut DmaHandle) {
    let Some(handle_ptr) = NonNull::new(handle) else {
        return;
    };
    unsafe { drop_in_place(handle_ptr.as_ptr()) };
    if let Err(e) = kfree(handle_ptr) {
        printk!(
            "WARNING: failed to free DMA handle in dma_unmap_single: {:?}\n",
            e
        );
    }
}

#[unsafe(export_name = "dma_handle_sync_for_device")]
pub extern "C" fn handle_sync_for_device_c(
    handle: *const DmaHandle,
    offset: usize,
    size: usize,
) -> i32 {
    let Some(handle) = (unsafe { handle.as_ref() }) else {
        return -1;
    };
    if let Err(e) = handle.sync_range_for_device(offset, size) {
        printk!("WARNING: dma_handle_sync_for_device failed: {:?}\n", e);
        return -1;
    }
    0
}

#[unsafe(export_name = "dma_handle_sync_for_cpu")]
pub extern "C" fn handle_sync_for_cpu_c(
    handle: *const DmaHandle,
    offset: usize,
    size: usize,
) -> i32 {
    let Some(handle) = (unsafe { handle.as_ref() }) else {
        return -1;
    };
    if let Err(e) = handle.sync_range_for_cpu(offset, size) {
        printk!("WARNING: dma_handle_sync_for_cpu failed: {:?}\n", e);
        return -1;
    }
    0
}

#[unsafe(export_name = "dma_handle_cpu_addr")]
pub extern "C" fn handle_cpu_addr_c(handle: *const DmaHandle) -> *mut c_void {
    let Some(handle) = (unsafe { handle.as_ref() }) else {
        return null_mut();
    };
    handle.cpu_addr().as_mut_ptr()
}

#[unsafe(export_name = "dma_handle_dma_addr")]
pub extern "C" fn handle_dma_addr_c(handle: *const DmaHandle) -> usize {
    let Some(handle) = (unsafe { handle.as_ref() }) else {
        return 0;
    };
    handle.dma_addr().as_usize()
}

#[unsafe(export_name = "dma_handle_size")]
pub extern "C" fn handle_size_c(handle: *const DmaHandle) -> usize {
    let Some(handle) = (unsafe { handle.as_ref() }) else {
        return 0;
    };
    handle.size()
}

#[unsafe(export_name = "dma_sync_sg_for_cpu")]
pub extern "C" fn sync_sg_for_cpu_c(
    device: *const Device,
    scatter_list: *mut DmaScatterList,
    n_entries: c_int,
    direction: c_int,
) {
    let Some((device, scatter_list)) = (unsafe { device.as_ref().zip(scatter_list.as_mut()) })
    else {
        return;
    };
    let Some(entries_count) = checked_entries_count(n_entries) else {
        return;
    };

    let Some(direction) = Direction::from_int(direction) else {
        printk!("WARNING: dma_sync_sg_for_cpu ignored invalid direction\n");
        return;
    };

    if let Err(e) = device.sync_sg_for_cpu(scatter_list, entries_count, direction) {
        printk!("WARNING: dma_sync_sg_for_cpu failed: {:?}\n", e);
    }
}

#[unsafe(export_name = "dma_sync_sg_for_device")]
pub extern "C" fn sync_sg_for_device_c(
    device: *const Device,
    scatter_list: *mut DmaScatterList,
    n_entries: c_int,
    direction: c_int,
) {
    let Some((device, scatter_list)) = (unsafe { device.as_ref().zip(scatter_list.as_mut()) })
    else {
        return;
    };
    let Some(entries_count) = checked_entries_count(n_entries) else {
        return;
    };

    let Some(direction) = Direction::from_int(direction) else {
        printk!("WARNING: dma_sync_sg_for_device ignored invalid direction\n");
        return;
    };

    if let Err(e) = device.sync_sg_for_device(scatter_list, entries_count, direction) {
        printk!("WARNING: dma_sync_sg_for_device failed: {:?}\n", e);
    }
}

#[unsafe(export_name = "dma_max_mapping_size")]
pub extern "C" fn max_mapping_size_c(device: *const Device) -> usize {
    let Some(device) = (unsafe { device.as_ref() }) else {
        return 0;
    };
    let max = device.constraints.max_segment_size as usize;
    if max == 0 { usize::MAX } else { max }
}

#[unsafe(export_name = "dma_opt_mapping_size")]
pub extern "C" fn opt_mapping_size_c(device: *const Device) -> usize {
    max_mapping_size_c(device)
}

#[unsafe(export_name = "dma_get_cache_alignment")]
pub extern "C" fn get_cache_alignment_c() -> usize {
    CACHELINE_SIZE
}

#[unsafe(export_name = "dma_get_required_mask")]
pub extern "C" fn get_required_mask_c(device: *const Device) -> usize {
    if unsafe { device.as_ref() }.is_none() {
        return 0;
    };
    let memory_size = TOTAL_PAGES
        .load(Ordering::Acquire)
        .saturating_mul(ArchPageTable::PAGE_SIZE);
    if memory_size <= 1 {
        return 0;
    }
    memory_size
        .checked_next_power_of_two()
        .unwrap_or(usize::MAX)
        .saturating_sub(1)
}

#[unsafe(export_name = "dma_get_merge_boundary")]
pub extern "C" fn get_merge_boundary_c(device: *const Device) -> usize {
    let Some(device) = (unsafe { device.as_ref() }) else {
        return 0;
    };
    device.constraints.boundary_mask
}

fn valid_dma_mask(mask: usize) -> bool {
    mask != 0
}

#[unsafe(export_name = "dma_set_mask")]
pub extern "C" fn set_mask_c(device: *mut Device, mask: usize) -> i32 {
    let Some(device) = (unsafe { device.as_mut() }) else {
        return -1;
    };
    if !valid_dma_mask(mask) {
        return -1;
    }
    device.constraints.mask = mask;
    device.coherent_allocator = None; // 重置 coherent_allocator，以便在下次分配时重新创建
    0
}

#[unsafe(export_name = "dma_set_coherent_mask")]
pub extern "C" fn set_coherent_mask_c(device: *mut Device, mask: usize) -> i32 {
    let Some(device) = (unsafe { device.as_mut() }) else {
        return -1;
    };
    if !valid_dma_mask(mask) {
        return -1;
    }
    device.constraints.coherent_mask = mask;
    device.coherent_allocator = None; // 重置 coherent_allocator，以便在下次分配时重新创建
    0
}

#[unsafe(export_name = "dma_set_mask_and_coherent")]
pub extern "C" fn set_mask_and_coherent_c(device: *mut Device, mask: usize) -> i32 {
    let Some(device) = (unsafe { device.as_mut() }) else {
        return -1;
    };
    if !valid_dma_mask(mask) {
        return -1;
    }
    device.constraints.mask = mask;
    device.constraints.coherent_mask = mask;
    device.coherent_allocator = None; // 重置 coherent_allocator，以便在下次分配时重新创建
    0
}

#[unsafe(export_name = "dma_set_max_seg_size")]
pub extern "C" fn set_max_seg_size_c(device: *mut Device, size: u32) -> i32 {
    let Some(device) = (unsafe { device.as_mut() }) else {
        return -1;
    };
    device.constraints.max_segment_size = size;
    device.coherent_allocator = None; // 重置 coherent_allocator，以便在下次分配时重新创建
    0
}

#[unsafe(export_name = "dma_get_max_seg_size")]
pub extern "C" fn get_max_seg_size_c(device: *const Device) -> u32 {
    let Some(device) = (unsafe { device.as_ref() }) else {
        return 0;
    };
    device.constraints.max_segment_size
}

#[unsafe(export_name = "dma_set_seg_boundary")]
pub extern "C" fn set_seg_boundary_c(device: *mut Device, mask: usize) -> i32 {
    let Some(device) = (unsafe { device.as_mut() }) else {
        return -1;
    };
    device.constraints.boundary_mask = mask;
    device.coherent_allocator = None; // 重置 coherent_allocator，以便在下次分配时重新创建
    0
}

#[unsafe(export_name = "dma_get_seg_boundary")]
pub extern "C" fn get_seg_boundary_c(device: *const Device) -> usize {
    let Some(device) = (unsafe { device.as_ref() }) else {
        return 0;
    };
    device.constraints.boundary_mask
}
