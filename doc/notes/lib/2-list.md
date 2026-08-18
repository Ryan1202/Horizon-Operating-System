`list.rs` 启发自 Linux 中使用的双向链表，目标是保证内存布局兼容的同时能够提供 Rusty 的使用方式

当然由于 Rust 在这方面还不是很成熟，所以使用了非常多的 nightly 特性

# 基础

## 定义

先看 Linux 的定义：

```c
struct list_head {
	struct list_head *next, *prev;
};
```

非常简单，两个指针分别指向前一个节点和后一个节点。但是放到 Rust 中有个严重的问题，那就是所有权和引用的问题，这也是 Rust 写链表的难点所在

一个方便的解决办法就是使用裸指针，这样可以不仅可以消除类型上的循环引用，还保持了和 `list_head` 的内存布局兼容

```rust
struct ListHead {
    prev: *mut ListHead,
    next: *mut ListHead,
}
```

更近一步，我们可以使用 `NonNull` ，要求指针永不为零，因为即使在 Linux 中，为空也不是一个有效状态

```rust
struct ListHead {
    prev: NonNull<ListHead>,
    next: NonNull<ListHead>,
}
```

`NonNull` 提供了和裸指针相同内存布局的保证：

> # Representation
>
> Thanks to the [null pointer optimization](https://doc.rust-lang.org/stable/core/option/index.html#representation), `NonNull<T>` and `Option<NonNull<T>>` are guaranteed to have the same size and alignment:
>
> ```rust
>use std::ptr::NonNull;
> 
>assert_eq!(size_of::<NonNull<i16>>(), size_of::<Option<NonNull<i16>>>());
> assert_eq!(align_of::<NonNull<i16>>(), align_of::<Option<NonNull<i16>>>());
>
> assert_eq!(size_of::<NonNull<str>>(), size_of::<Option<NonNull<str>>>());
> assert_eq!(align_of::<NonNull<str>>(), align_of::<Option<NonNull<str>>>());
> ```

但为了更方便和更安全的使用，链表头和链表节点不能使用相同的类型，而为了两者具有相同的内存布局，所以单独抽出了一个类型

```rust
struct Link {
    prev: NonNull<Link>,
    next: NonNull<Link>,
}

struct ListHead {
    link: Link,
}

struct ListNode {
    link: Option<Link>,
}
```

`ListNode` 使用 `Option::None` 来表示还没加入链表，同样得益于空指针优化，`ListNode` 还能保证内存布局不变

这里就碰上了另一个问题，`ListHead` 在刚初始化时是一个自引用结构，而 Rust 要求变量必须初始化了才能使用，碰上了先有鸡还是先有蛋的问题。我选择了使用悬垂指针 `NonNull::dangling` 来表示未初始化状态，它的值等于指针指向的类型要求对齐的字节数，所以除非第 0 页有效，否则不可能是一个有效的指针

再进一步，从类型上就要保证 `ListNode` 和 `ListHead` 认为的容器类型是相同的，避免出现 UB

```rust
#[repr(C)]
struct Link<Owner> {
    prev: NonNull<Link<Owner>>,
    next: NonNull<Link<Owner>>,
}

#[repr(transparent)]
struct ListHead<Owner> {
    link: Link<Owner>,
}

#[repr(transparent)]
struct ListNode<Owner> {
    link: Option<Link<Owner>>,
}
```

## 功能

### 初始化

首先是最基础的初始化

对于链表头来说首先需要一个用于定义全局变量的为初始化默认值，真正初始化的时候需要创建一个自引用

```rust
const impl<Owner> Default for ListHead<Owner> {
    fn default() -> Self {
        Self {
            link: Link::new(NonNull::dangling(), NonNull::dangling()),
        }
    }
}

impl<Owner> ListHead<Owner> {
    #[inline(always)]
    pub unsafe fn init(&mut self) {
        let ptr = unsafe { NonNull::new_unchecked(&raw mut self.link) };

        self.link = Link::new(ptr, ptr);
    }
}
```

为了绕过借用检查，先获取其裸指针再转换成 `NonNull` 写入，`write` 是 `MaybeUninit` 提供的一种初始化方式

---

对于链表节点来说就简单了，直接一个 `None` 常量就搞定

```rust
pub const fn new() -> Self {
    Self { link: None }
}
```

## 添加节点

首先 `ListNode` 需要提供一个底层方法直接修改链表

```rust
/// 将当前节点添加到`prev`和`next`之间
///
/// # Safety
///
/// 当前节点的`link`必须为`None`，否则会导致链表结构损坏
#[inline(always)]
unsafe fn add(&mut self, mut prev: NonNull<Link<Owner>>, mut next: NonNull<Link<Owner>>) {
    debug_assert!(self.link.is_none(), "trying to add an already linked node");
    self.link = Some(Link::new(prev, next));

    unsafe {
        let _self = NonNull::new_unchecked(self.link.as_mut().unwrap());
        prev.as_mut().next = _self;
        next.as_mut().prev = _self;
    };
}
```

由于 `Link` 中的指针类型是 `NonNull<Link<Owner>>` 不是 `Option<NonNull<Link<Owner>>>` 所以要先 `.as_mut().unwrap()` 

在 `add` 的基础上再封装出可以调用的版本，负责拿到前后两个节点之后再调用 `add`

```rust
/// 将当前节点添加到`node`节点之后
#[inline(always)]
pub unsafe fn add_after(&mut self, node: &mut Self) {
    unsafe {
        let node = node
            .link
            .as_mut()
            .expect("trying to add_after an unlinked node");

        let next = node.next;
        let prev = NonNull::from_mut(node);

        self.add(prev, next);
    }
}

/// 将当前节点添加到`node`节点之前
#[inline(always)]
pub unsafe fn add_before(&mut self, node: &mut Self) {
    unsafe {
        let node = node
            .link
            .as_mut()
            .expect("trying to add_before an unlinked node");

        let prev = node.prev;
        let next = NonNull::from_mut(node);

        self.add(prev, next);
    }
}
```

所以再进一步在 `ListHead` 上可以安全地对外提供 `add_head` 和 `add_tail`

```rust
#[inline(always)]
pub fn add_head(&mut self, node: &mut ListNode<Owner>) {
    unsafe {
        let prev = NonNull::new_unchecked(&raw mut self.link);
        let next = self.link.next;
        
        node.add(prev, next);
    }
}

#[inline(always)]
pub fn add_tail(&mut self, node: &mut ListNode<Owner>) {
    unsafe {
        let prev = self.link.prev;
        let next = NonNull::new_unchecked(&raw mut self.link);
        
        node.add(prev, next);
    }
}
```

## 删除节点

删除节点也差不多，不同的是删除的方法要通过链表头调用避免出现竞争问题

```rust
#[inline(always)]
unsafe fn delete(&mut self) {
    unsafe {
        let link = self
            .link
            .as_mut()
            .expect("trying to delete an unlinked list node");

        link.prev.as_mut().next = link.next;
        link.next.as_mut().prev = link.prev;

        self.link = None;
    }
}
```

```rust
#[inline(always)]
pub unsafe fn delete(&mut self, node: &mut ListNode<Owner>) {
    unsafe {
        node.delete();
    }
}
```

# 进阶：迭代器

想要更 Rusty，可以为链表实现迭代器，这样就可以通过 `for node in list_head.iter() {}` 的方式遍历链表了

首先是定义用于迭代器的类型

```rust
pub struct ListIterator<'a, Owner> {
    head: NonNull<Link<Owner>>,
    next: Option<NonNull<Link<Owner>>>,
    offset: usize,
    _phantom: PhantomData<&'a ListHead<Owner>>,
}
```

由于要从链表地址恢复到容器类型的地址，需要保存容器类型的开头到链表字段的偏移地址。然后就可以实现迭代器方法了

```rust
impl<Owner> Iterator for ListIterator<Owner> {
    type Item = NonNull<Owner>;

    fn next(&mut self) -> Option<Self::Item> {
        let current = self.next;
        self.next = current
            .map(|current| unsafe { current.as_ref().next })
            // 判断是否到达尾节点
            .and_then(|next| (next != self.head).then_some(next));

        // 转换到 Owner 类型
        current.map(|p| unsafe { p.byte_offset(self.offset).cast() })
    }
}
```

`Iterator` 是一个 trait，其中要求必须实现的有返回的类型 `Item` 和获取下一个 `Item` 的 `next`，其他还有很多方法是利用 `next` 的默认实现，如果有可以加速的场景也可以覆盖默认实现

创建迭代器的方法是自己定义的，当然也可以实现 `IntoIterator` trait 来自动转换成迭代器

```rust
impl<Owner> ListHead<Owner> {
    pub fn iter(&self, offset: usize) -> ListIterator<Owner> {
        debug_assert!(
            self.link.prev != NonNull::dangling() && self.link.next != NonNull::dangling(),
            "ListHead is not initialized"
        );

        let head = NonNull::from_ref(&raw const self.link);
        let first = self.link.next;

        ListIterator {
            head,
            next: if first != head { Some(first) } else { None },
            offset: -(offset as isize),
            _phantom: PhantomData,
        }
    }
}
```

使用则是像这样：

```rust
for node in list_head.iter(offset_of!(node_field, ContainerType)) {
    // ...
}
```

不需要像 Linux 那样依赖 UB，Rust 提供了一个宏 `offset_of!` 用来获取一个字段在类型中的偏移

但是在使用迭代器时手动传入偏移不仅麻烦，而且无法保证传入的值是否正确

# 再进一步：编译期反射

编译期反射是最近一段时间 Rust 相对比较热门的话题，不过说起来高级，其实对于链表来说只需要用到其中的 `field_projections`  特性自动获取每个字段的偏移即可

目前 `field_projections` 还是一个不完整特性，需要在根文件声明时额外加一条允许启用不完整特性

```rust
#![allow(incomplete_features)]
#![feature(field_projections)]
```

## Field

`core` crate 提供了 `Field` trait

```rust
pub unsafe trait Field: Send + Sync + Copy {
    /// The type of the base where this field exists in.
    #[lang = "field_base"]
    type Base;

    /// The type of the field.
    #[lang = "field_type"]
    type Type;

    /// The offset of the field in bytes.
    #[lang = "field_offset"]
    const OFFSET: usize = crate::intrinsics::field_offset::<Self>();
}
```

可以看到我们需要用的容器类型、字段类型以及字段偏移都齐了

另外还有一个 `field_of!` 宏，返回的是一个实现了 `Field` 的类型，但是目前 ra 的自动类型推导还不支持这个，只能知道它是一个类型，而且只能使用 `Field` trait 中的信息

但是 `field_of!` 只支持一层类型，所以我又设计了一个 `field_path` 可以跨越多个类型包装

首先是定义了 `FieldPath` 来包装 `Field`

```rust
pub const unsafe trait FieldPath {
    type Base;
    type Target;

    const OFFSET: usize;

    unsafe fn project(base: NonNull<Self::Base>) -> NonNull<Self::Target> {
        unsafe { base.byte_offset(Self::OFFSET as isize) }.cast()
    }

    unsafe fn unproject(target: NonNull<Self::Target>) -> NonNull<Self::Base> {
        unsafe { target.byte_offset(-(Self::OFFSET as isize)) }.cast()
    }
}

const unsafe impl<F: Field> FieldPath for F {
    type Base = F::Base;
    type Target = F::Type;

    const OFFSET: usize = F::OFFSET;
}
```

然后通过零大小类型 `Then<A, B>` 来把多个 `field_of!` 的返回值拼起来

```rust
pub struct Then<A, B>(PhantomData<fn() -> (A, B)>);

const unsafe impl<A, B> FieldPath for Then<A, B>
where
    A: FieldPath,
    B: FieldPath<Base = A::Target>,
{
    type Base = A::Base;
    type Target = B::Target;

    const OFFSET: usize = A::OFFSET + B::OFFSET;
}
```

最后是 `field_path!` 宏方便使用

```rust
pub macro field_path {
    ($base:ty => $field:ident $(,)?) => {
        core::field::field_of!($base, $field)
    },
    ($base:ty => $field:ident, $($rest:tt)+) => {
        Then<
            core::field::field_of!($base, $field),
            field_path!($($rest)+),
        >
    }
}
```

## ListMember

回到链表，定义了 `ListMember` trait用来再包装 `FieldPath`，提供直属类型和上层类型，还有转换的方法

```rust
pub const unsafe trait ListMember: FieldPath {
    type Owner;
    type LinkOwner;

    unsafe fn node_of(owner: NonNull<Self::Owner>) -> NonNull<ListNode<Self::LinkOwner>>;
    unsafe fn owner_of(node: NonNull<ListNode<Self::LinkOwner>>) -> NonNull<Self::Owner>;
}
```

## ListNodeStorage

由于 `ListNode` 不一定是直接使用的，可能是 `SyncUnsafeCell<ListNode<T>>` ，所以抽出了直接访问到链表节点的方法

```rust
const unsafe impl<Owner> ListNodeStorage for ListNode<Owner> {
    type LinkOwner = Owner;

    unsafe fn node_ptr(storage: NonNull<Self>) -> NonNull<ListNode<Owner>> {
        storage
    }

    unsafe fn storage_ptr(node: NonNull<ListNode<Owner>>) -> NonNull<Self> {
        node
    }
}

const unsafe impl<Owner> ListNodeStorage for UnsafeCell<ListNode<Owner>> {
    type LinkOwner = Owner;

    unsafe fn node_ptr(storage: NonNull<Self>) -> NonNull<ListNode<Owner>> {
        storage.cast::<ListNode<Owner>>()
    }

    unsafe fn storage_ptr(node: NonNull<ListNode<Owner>>) -> NonNull<Self> {
        node.cast::<UnsafeCell<ListNode<Owner>>>()
    }
}

const unsafe impl<Owner> ListNodeStorage for SyncUnsafeCell<ListNode<Owner>> {
    type LinkOwner = Owner;

    unsafe fn node_ptr(storage: NonNull<Self>) -> NonNull<ListNode<Owner>> {
        storage.cast::<ListNode<Owner>>()
    }

    unsafe fn storage_ptr(node: NonNull<ListNode<Owner>>) -> NonNull<Self> {
        node.cast::<SyncUnsafeCell<ListNode<Owner>>>()
    }
}

const unsafe impl<P> ListMember for P
where
    P: const FieldPath,
    P::Target: const ListNodeStorage,
{
    type Owner = P::Base;
    type LinkOwner = <P::Target as ListNodeStorage>::LinkOwner;

    unsafe fn node_of(owner: NonNull<Self::Owner>) -> NonNull<ListNode<Self::LinkOwner>> {
        let storage = unsafe { P::project(owner) };
        unsafe { <P::Target as ListNodeStorage>::node_ptr(storage) }
    }

    unsafe fn owner_of(node: NonNull<ListNode<Self::LinkOwner>>) -> NonNull<Self::Owner> {
        let storage = unsafe { <P::Target as ListNodeStorage>::storage_ptr(node) };
        unsafe { P::unproject(storage) }
    }
}
```

最后一个 impl 可以自动为处于某个类型的字段中的链表节点实现 `ListMember` ，避免了手动实现，从而无感使用

## 类型定义

 `Link<Owner>` 和 `ListNode<Owner>` 中的 `Owner` 只能是直接的外面一层的类型，而 `ListHead` 允许多层嵌套用于遍历时方便地转换回外层类型

```rust
#[repr(transparent)]
pub struct ListHead<Member: ListMember> {
    link: Link<Member::LinkOwner>,
}
```

## 实现

所以一些实现也需要变化

```rust
#[repr(transparent)]
pub struct ListHead<Member: ListMember> {
    link: Link<Member::LinkOwner>,
}

const impl<T: ListMember> Default for ListHead<T> {
    fn default() -> Self {
        Self {
            link: Link::new(NonNull::dangling(), NonNull::dangling()),
        }
    }
}

impl<Member: ListMember> ListHead<Member> {
    #[inline(always)]
    pub unsafe fn init(&mut self) {
        let ptr = unsafe { NonNull::new_unchecked(&raw mut self.link) };

        self.link = Link::new(ptr, ptr);
    }

    #[inline(always)]
    pub fn add_head(&mut self, owner: &mut Member::Owner) {
        unsafe {
            let prev = NonNull::new_unchecked(&raw mut self.link);
            let next = self.link.next;
            let mut node = Member::node_of(NonNull::from_mut(owner));
            node.as_mut().add(prev, next);
        }
    }

    #[inline(always)]
    pub unsafe fn add_head_ref(&mut self, owner: &Member::Owner) {
        unsafe {
            let prev = NonNull::new_unchecked(&raw mut self.link);
            let next = self.link.next;
            let mut node = Member::node_of(NonNull::from_ref(owner));
            node.as_mut().add(prev, next);
        }
    }

    #[inline(always)]
    pub fn add_tail(&mut self, owner: &mut Member::Owner) {
        unsafe {
            let prev = self.link.prev;
            let next = NonNull::new_unchecked(&raw mut self.link);
            let mut node = Member::node_of(NonNull::from_mut(owner));
            node.as_mut().add(prev, next);
        }
    }

    #[inline(always)]
    pub unsafe fn delete(&mut self, owner: &mut Member::Owner) {
        unsafe {
            Member::node_of(NonNull::from_mut(owner)).as_mut().delete();
        }
    }

    #[inline(always)]
    pub unsafe fn delete_ref(&mut self, owner: &Member::Owner) {
        unsafe {
            Member::node_of(NonNull::from_ref(owner)).as_mut().delete();
        }
    }

    #[inline(always)]
    pub fn is_empty(&self) -> bool {
        debug_assert!(
            self.link.prev != NonNull::dangling() && self.link.next != NonNull::dangling(),
            "ListHead is not initialized"
        );
        let ptr = NonNull::from_ref(self).cast();

        self.link.prev == ptr && self.link.next == ptr
    }
}
```

其中 `*_ref` 标记为 `unsafe` ，只用于只能拿到不可变引用的类型的情况，因为逻辑上链表所以节点的所有权归链表头管理

## 迭代器

迭代器也需要相应的修改，并且可以返回外层的类型

```rust
pub struct ListIterator<'a, Member: ListMember> {
    head: NonNull<Link<Member::LinkOwner>>,
    next: Option<NonNull<Link<Member::LinkOwner>>>,
    _phantom: PhantomData<&'a ListHead<Member>>,
}

impl<Member: ListMember> ListHead<Member> {
    pub unsafe fn iter(&self) -> ListIterator<'_, Member> {
        debug_assert!(
            self.link.prev != NonNull::dangling() && self.link.next != NonNull::dangling(),
            "ListHead is not initialized"
        );

        let first = self.link.next;

        let head = NonNull::from_ref(&self.link);

        ListIterator {
            head,
            next: if first != head { Some(first) } else { None },
            _phantom: PhantomData,
        }
    }
}

impl<Member: ListMember> Iterator for ListIterator<'_, Member> {
    type Item = NonNull<Member::Owner>;

    fn next(&mut self) -> Option<Self::Item> {
        let current = self.next;
        self.next = current
            .map(|current| unsafe { current.as_ref().next })
            // 判断是否到达尾节点
            .and_then(|next| (next != self.head).then_some(next));

        // 转换到 Owner 类型
        current
            .map(|p| p.cast::<ListNode<Member::LinkOwner>>())
            .map(|p| unsafe { Member::owner_of(p) })
    }
}
```

