/// Red zone 标记字节
const RED_ZONE_BYTE: u8 = 0xBB;

/// 释放后填充字节（检测 use-after-free）
const POISON_FREE: u8 = 0x6B;

/// 分配后填充字节（检测使用未初始化内存）
const POISON_ALLOC: u8 = 0x5A;

/// Red zone 大小（字节）——对象前后各这么多
pub(super) const RED_ZONE_SIZE: usize = 16;

/// 将 slab 分配的原始指针偏移到用户对象起始位置
///
/// Layout: [RED_ZONE | USER_OBJECT | RED_ZONE]
pub const fn user_ptr_offset(_align: usize) -> usize {
    #[cfg(feature = "slub_debug")]
    {
        RED_ZONE_SIZE.max(_align)
    }

    #[cfg(not(feature = "slub_debug"))]
    {
        0
    }
}

/// 初始化对象的 red zone
pub unsafe fn init_red_zones(slab_obj_start: *mut u8, user_offset: usize, user_size: usize) {
    // 前 red zone
    unsafe {
        core::ptr::write_bytes(slab_obj_start, RED_ZONE_BYTE, user_offset);
    }
    // 后 red zone
    unsafe {
        core::ptr::write_bytes(
            slab_obj_start.add(user_offset + user_size),
            RED_ZONE_BYTE,
            RED_ZONE_SIZE,
        );
    }
}

/// 检查 red zone 完整性
///
/// 返回 true 表示 red zone 完好，false 表示被破坏
pub unsafe fn check_red_zones(
    slab_obj_start: *mut u8,
    user_offset: usize,
    user_size: usize,
) -> bool {
    // 检查前 red zone
    for i in 0..user_offset {
        if unsafe { *slab_obj_start.add(i) } != RED_ZONE_BYTE {
            report_corruption("front red zone", slab_obj_start, i);
            return false;
        }
    }
    // 检查后 red zone
    let back_start = unsafe { slab_obj_start.add(user_offset + user_size) };
    for i in 0..RED_ZONE_SIZE {
        if unsafe { *back_start.add(i) } != RED_ZONE_BYTE {
            report_corruption("back red zone", slab_obj_start, user_offset + user_size + i);
            return false;
        }
    }
    true
}

/// 释放时填充 poison 字节
pub unsafe fn poison_on_free(user_ptr: *mut u8, user_size: usize) {
    unsafe {
        core::ptr::write_bytes(user_ptr, POISON_FREE, user_size);
    }
}

/// 分配时填充 poison 字节（检测使用未初始化内存）
pub unsafe fn poison_on_alloc(user_ptr: *mut u8, user_size: usize) {
    unsafe {
        core::ptr::write_bytes(user_ptr, POISON_ALLOC, user_size);
    }
}

/// 检查释放后的对象是否被修改（use-after-free 检测）
pub unsafe fn check_poison(user_ptr: *mut u8, user_size: usize) -> bool {
    for i in 0..user_size {
        // FreeNode 的前几个字节用于 next 指针，不检查
        if i < core::mem::size_of::<usize>() {
            continue;
        }
        if unsafe { *user_ptr.add(i) } != POISON_FREE {
            report_use_after_free(user_ptr, i);
            return false;
        }
    }
    true
}

fn report_corruption(zone: &str, obj_start: *mut u8, offset: usize) {
    printk!(
        "SLUB DEBUG: {} corruption at object {:p}, offset {:#x}\n",
        zone,
        obj_start,
        offset,
    );
}

fn report_use_after_free(user_ptr: *mut u8, offset: usize) {
    printk!(
        "SLUB DEBUG: use-after-free at {:p}, offset {:#x} (expected {:#x}, found {:#x})\n",
        user_ptr,
        offset,
        POISON_FREE,
        unsafe { *user_ptr.add(offset) },
    );
}
