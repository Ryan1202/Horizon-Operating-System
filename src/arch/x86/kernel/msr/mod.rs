macro_rules! define_msr {
    ($($name:ident = $value:expr),+ $(,)?) => {
        $(
            #[allow(unused)]
            pub const ${concat(IA32_, $name)}: u32 = $value;
        )+
    };
}

mod architectural;

pub use architectural::*;
