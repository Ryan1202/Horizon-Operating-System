use core::{
    field::Field,
    marker::PhantomData,
    mem::{
        align_of,
        type_info::{self, TypeKind},
    },
    ptr::NonNull,
};

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

#[derive(Debug)]
pub struct Then<A, B>(PhantomData<fn() -> (A, B)>);

const impl<A, B> Default for Then<A, B> {
    fn default() -> Self {
        Self(PhantomData)
    }
}

const unsafe impl<A, B> FieldPath for Then<A, B>
where
    A: FieldPath,
    B: FieldPath,
{
    type Base = A::Base;
    type Target = B::Target;

    const OFFSET: usize = {
        let mut storage = type_info::of::<A::Target>();
        let base = type_info::of::<B::Base>();

        assert!(
            align_of::<A::Target>() >= align_of::<B::Base>(),
            "unaligned field-path base"
        );

        while storage != base {
            // 透明类型检查
            assert!(
                matches!(storage.info().kind, TypeKind::Struct) && storage.fields(0) == 1,
                "incompatible field-path segments",
            );
            let field = storage.field(0, 0);
            assert!(field.offset() == 0, "nonzero field-path wrapper offset");
            storage = field.type_id();
        }

        A::OFFSET + B::OFFSET
    };
}

/// Composes `Type => field` segments and existing `FieldPath` types.
/// Adjacent segments can pass through single-field struct wrappers at offset zero,
/// provided the storage is aligned for the next segment's base type.
/// Access through the resulting path still requires appropriate synchronization.
pub macro field_path {
    ($base:ty => $field:ident $(,)?) => {
        core::field::field_of!($base, $field)
    },
    ($base:ty => $field:ident, $($rest:tt)+) => {
        Then<
            core::field::field_of!($base, $field),
            field_path!($($rest)+),
        >
    },
    ($path:ty $(,)?) => {
        $path
    },
    ($path:ty, $($rest:tt)+) => {
        Then<$path, field_path!($($rest)+)>
    }
}
