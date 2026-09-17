x86 每个 CPU 核心都有自己独立的中断向量号资源，需要被管理和动态分配，因此我设计了 `VectorManager` 来统一管理。

# 结构

具体到实现细节上，中断向量号使用位图结构来管理，每个向量号占用 1 bit，我使用了 `BitSet` 来抽象最基础的位图操作。由于 x86 每个核心最多只有 256 个中断向量号，所以每个核心只需要 $256 \div 8 = 32$ 字节来保存

```rust
pub struct VectorManager {
    inner: Spinlock<MaybeUninit<VectorInner>>,
}

struct VectorInner {
    /// 每个 CPU 一份的管理结构
    maps: PerCpuDyn<VectorMap>,
    /// 已全局分配的向量
    global: BitSet<[usize; MAX_VECTOR_COUNT / usize::BITS as usize]>,
}

struct VectorMap {
    /// 当前 CPU 的 APIC ID
    apic_id: ApicId,
    /// 剩余可用的中断向量号个数
    available: usize,
    /// 每个中断向量号的分配情况
    map: BitSet<[usize; MAX_VECTOR_COUNT / usize::BITS as usize]>,
    /// 每个中断向量号对应的全局 IRQ 编号，在 `map` 对应的位设置了之后才有效
    irqs: [IrqNumber; MAX_VECTOR_COUNT],
}
```

`PerCpuDyn<VectorMap>` 表示 `VectorMap` 是每个 CPU 都有一份自己专属的数据的，而 `global` 则是用来管理在所有 CPU 上都被分配的向量号，因此在外层需要通过 `Spinlock` 来避免竞争。

在 `VectorMap` 中，`apic_id` 用来保存当前 Local APIC 的 id 便于区分；`available` 用来表示剩余可用的中断向量号个数；`map` 则是标识每个中断向量号是否已被使用；`irqs` 用来在发生中断时快速的反查对应的 IRQ，使用 `INVALID_IRQ` 所谓占位的默认值 ，在运行时通过检查 `map` 确认是否被分配之后才访问 `irqs` 。

# 初始化

首先，加锁，获取每个 CPU 的信息（主要是 APIC ID）

```rust
let mut guard = self.inner.lock_irqsave();
let cpus = X86Topology::get().cpus();
```

初始化每个 CPU 的数据

```rust
let maps = PerCpuDyn::try_new_with(|cpuid| {
    let mut map = VectorMap {
        apic_id: cpus[cpuid.get() as usize].id(),
        available: 0,
        map: BitSet::zeroed(MAX_VECTOR_COUNT),
        irqs: [MaybeUninit::uninit(); MAX_VECTOR_COUNT],
    };
    for vector in 0..MAX_VECTOR_COUNT {
        if reserved(vector) {
            map.map.set(vector);
        } else {
            map.available += 1;
        }
    }
    map
})?;
```

写入数据

```rust
pub(super) fn reserve(&self, irq: IrqNumber, vector: u8) -> Result<(), IrqError> {
    let mut guard = self.inner.lock_irqsave();
    let state = unsafe { guard.assume_init_mut() };
    let maps = &mut state.maps;

    if vector < 0x20 || vector >= FIRST_DEVICE_VECTOR {
        return Err(IrqError::InvalidArgument);
    }

    for (_, map) in maps.iter_mut()? {
        map.map.set(vector as usize);
        map.irqs[vector as usize] = irq;
    }
    Ok(())
}
```

# 上线 CPU 后的注册

首先加锁后获取相应 CPU 的管理结构

```rust
let guard = self.inner.lock_irqsave();

let inner = unsafe { guard.assume_init_ref() };
let maps = &inner.maps;

let map = maps
    .get_remote_mut(cpu_id)
    .expect("failed to get vector map when registering CPU");
```

设置 APIC ID，然后从 `global` 同步 irq 分配信息

```rust
map.apic_id = apic_id;

unsafe { copy_nonoverlapping(&inner.global, &mut map.map, 1) };
```

# 分配中断向量号

首先还是加锁后获取 `maps`

```rust
let mut guard = self.inner.lock_irqsave();

let state = unsafe { guard.assume_init_mut() };
let maps = &state.maps;
```

对于 0-15 的 IRQ 是 x86 专门给 ISA 预留的 IRQ 号，直接修改反查表就够了

```rust
if irq.get() < 16 {
    let vector = 0x20 + irq.get();
    let cpu = match affinity {
        Affinity::Auto => CpuId::new(0),
        Affinity::Cpu(cpu) => cpu,
    };
    let map = maps.get_remote_mut(cpu).ok_or(IrqError::NotFound)?;
    return Ok(VectorRoute {
        cpu,
        apic_id: map.apic_id,
        vector: vector as u8,
    });
}
```

对于其他 IRQ，如果是全局向量则需要找一个所有 CPU 都空闲的中断向量号

```rust
if scope == VectorScope::Global {
    let cpu = match affinity {
        Affinity::Auto => CpuId::new(0),
        Affinity::Cpu(cpu) => cpu,
    };
    let apic_id = maps.get_remote(cpu).ok_or(IrqError::NotFound)?.apic_id;

    let mut selected: Option<usize> = None;
    'outer: for vector in 0..MAX_VECTOR_COUNT {
        if reserved(vector) || state.global.test(vector) {
            continue;
        }

        for (_, map) in maps.iter()? {
            if map.available == 0 {
                return Err(IrqError::OutOfIrq);
            }
            if map.map.test(vector) {
                continue 'outer;
            }
        }

        selected = Some(vector);
        break;
    }

    let vector = selected.ok_or(IrqError::OutOfIrq)?;
    state.global.set(vector);

    for (_, map) in maps.iter_mut()? {
        map.map.set(vector);
        map.available -= 1;
        map.irqs[vector] = irq;
    }

    Ok(VectorRoute {
        cpu,
        apic_id,
        vector: vector as u8,
    })
}
```

否则，只需要找出所有 CPU 中可用中断向量号最多的一个分配就好了

```rust
else {
    // 尚未实现在线 CPU 跟踪和负载均衡；Auto 固定使用 BSP
    let cpu = match affinity {
        Affinity::Auto => CpuId::new(0),
        Affinity::Cpu(cpu) => cpu,
    };

    let map = maps.get_remote_mut(cpu).ok_or(IrqError::NotFound)?;

    let vector = map.map.find_first_zero().ok_or(IrqError::OutOfIrq)?;

    map.map.set(vector);
    map.available -= 1;
    map.irqs[vector] = irq;

    Ok(VectorRoute {
        cpu,
        apic_id: map.apic_id,
        vector: vector as u8,
    })
}
```

如果返回的 `VectorRoute` 里 `cpu` / `apic_id` 是 `None` 表示是全局向量号

# 释放

释放也是需要区分这三种情况

```rust
pub(crate) fn free(&self, route: VectorRoute) {
    let mut guard = self.inner.lock_irqsave();
    let state = unsafe { guard.assume_init_mut() };
    let maps = &state.maps;

    if route.vector < FIRST_DEVICE_VECTOR {
        return;
    }

    let vector = route.vector as usize;

    if state.global.test(vector) {
        state.global.clear(vector);

        let maps = &mut state.maps;
        for (_, map) in maps.iter_mut().expect("failed to iterate vector maps") {
            map.map.clear(vector);
            map.available += 1;
            map.irqs[vector] = INVALID_IRQ;
        }
    } else {
        let map = maps
            .get_remote_mut(route.cpu)
            .expect("failed to get vector map for CPU");

        map.irqs[vector] = INVALID_IRQ;

        map.map.clear(vector);
        map.available += 1;
    }
}
```

# 查询

可以通过 `CpuId` 或者 `ApicId` 两种方式来查找指定 `vector` 对应的 `IrqNumber`

```rust
/// 入口传入当前逻辑 CPU 和原始 IDT vector，不接受 ISA IRQ 编号。
pub fn lookup(&self, cpu: CpuId, vector: u8) -> Option<IrqNumber> {
    let guard = self.inner.lock_irqsave();
    let maps = &unsafe { guard.assume_init_ref() }.maps;

    let map = maps.get_remote(cpu)?;

    let irq = map.irqs[vector as usize];
    (irq != INVALID_IRQ).then_some(irq)
}

/// LAPIC 身份在当前 CPU 的短临界区内取得；这里只读取固定的 CPU 注册表。
pub(super) fn lookup_apic(&self, apic_id: ApicId, vector: u8) -> Option<IrqNumber> {
    let guard = self.inner.lock_irqsave();
    let maps = &unsafe { guard.assume_init_ref() }.maps;

    for (_, map) in maps.iter().ok()? {
        if map.apic_id == apic_id {
            let irq = map.irqs[vector as usize];
            return (irq != INVALID_IRQ).then_some(irq);
        }
    }

    None
}
```

