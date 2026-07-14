#[macro_export]
macro_rules! impl_addr {
    ($name:ident) => {
        impl $name {
            pub const fn offset_from(self, other: Self) -> Option<usize> {
                self.0.checked_sub(other.0)
            }
        }

        const impl core::cmp::PartialEq for $name {
            fn eq(&self, other: &Self) -> bool {
                self.0 == other.0
            }
        }

        const impl core::cmp::PartialOrd for $name {
            fn partial_cmp(&self, other: &Self) -> Option<core::cmp::Ordering> {
                self.0.partial_cmp(&other.0)
            }
        }

        impl core::fmt::Pointer for $name {
            fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
                core::fmt::Pointer::fmt(&(self.0 as *const usize), f)
            }
        }

        const impl core::ops::Add<usize> for $name {
            type Output = Self;

            fn add(self, rhs: usize) -> Self::Output {
                Self(self.0 + rhs)
            }
        }

        const impl core::ops::AddAssign<usize> for $name {
            fn add_assign(&mut self, rhs: usize) {
                self.0 += rhs;
            }
        }

        const impl core::ops::Sub<usize> for $name {
            type Output = Self;

            fn sub(self, rhs: usize) -> Self::Output {
                Self(self.0 - rhs)
            }
        }

        const impl core::ops::Sub for $name {
            type Output = usize;

            fn sub(self, rhs: Self) -> Self::Output {
                self.0 - rhs.0
            }
        }

        const impl core::ops::SubAssign<usize> for $name {
            fn sub_assign(&mut self, rhs: usize) {
                self.0 -= rhs;
            }
        }

        impl core::fmt::Display for $name {
            fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
                write!(f, "0x{:016x}", self.0)
            }
        }
    };
}

#[macro_export]
macro_rules! impl_page_addr {
    ($name:ident, $size:expr) => {
        crate::impl_addr!($name);

        impl $name {
            pub const fn is_page_aligned(&self) -> bool {
                self.0.is_multiple_of($size)
            }

            pub const fn page_align_down(self) -> Self {
                Self(self.0 & !($size - 1))
            }

            pub const fn page_align_up(self) -> Self {
                Self(self.0.next_multiple_of($size))
            }

            pub const fn page_offset(self) -> usize {
                self.0 % $size
            }
        }
    };
}
