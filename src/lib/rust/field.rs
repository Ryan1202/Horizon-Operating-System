use core::{field::Field, marker::PhantomData, ptr::NonNull};

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
    B: FieldPath<Base = A::Target>,
{
    type Base = A::Base;
    type Target = B::Target;

    const OFFSET: usize = A::OFFSET + B::OFFSET;
}

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
