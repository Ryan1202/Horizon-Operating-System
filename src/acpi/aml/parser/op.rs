use crate::acpi::aml::{Bytecode, parser::prefix};

pub const ZERO_OP: u8 = 0x00;
pub const ONE_OP: u8 = 0x01;
pub const ALIAS_OP: u8 = 0x06;
pub const NAME_OP: u8 = 0x08;
pub const SCOPE_OP: u8 = 0x10;
pub const BUFFER_OP: u8 = 0x11;
pub const PACKAGE_OP: u8 = 0x12;
pub const VAR_PACKAGE_OP: u8 = 0x13;
pub const METHOD_OP: u8 = 0x14;
pub const EXTERNAL_OP: u8 = 0x15;
pub const LOCAL0_OP: u8 = 0x60;
pub const LOCAL1_OP: u8 = 0x61;
pub const LOCAL2_OP: u8 = 0x62;
pub const LOCAL3_OP: u8 = 0x63;
pub const LOCAL4_OP: u8 = 0x64;
pub const LOCAL5_OP: u8 = 0x65;
pub const LOCAL6_OP: u8 = 0x66;
pub const LOCAL7_OP: u8 = 0x67;
pub const ARG0_OP: u8 = 0x68;
pub const ARG1_OP: u8 = 0x69;
pub const ARG2_OP: u8 = 0x6A;
pub const ARG3_OP: u8 = 0x6B;
pub const ARG4_OP: u8 = 0x6C;
pub const ARG5_OP: u8 = 0x6D;
pub const ARG6_OP: u8 = 0x6E;
pub const STORE_OP: u8 = 0x70;
pub const REF_OF_OP: u8 = 0x71;
pub const ADD_OP: u8 = 0x72;
pub const CONCAT_OP: u8 = 0x73;
pub const SUBTRACT_OP: u8 = 0x74;
pub const INCREMENT_OP: u8 = 0x75;
pub const DECREMENT_OP: u8 = 0x76;
pub const MULTIPLY_OP: u8 = 0x77;
pub const DIVIDE_OP: u8 = 0x78;
pub const SHIFT_LEFT_OP: u8 = 0x79;
pub const SHIFT_RIGHT_OP: u8 = 0x7A;
pub const AND_OP: u8 = 0x7B;
pub const NAND_OP: u8 = 0x7C;
pub const OR_OP: u8 = 0x7D;
pub const NOR_OP: u8 = 0x7E;
pub const XOR_OP: u8 = 0x7F;
pub const NOT_OP: u8 = 0x80;
pub const FIND_SET_LEFT_BIT_OP: u8 = 0x81;
pub const FIND_SET_RIGHT_BIT_OP: u8 = 0x82;
pub const DEREF_OF_OP: u8 = 0x83;
pub const CONCAT_RES_OP: u8 = 0x84;
pub const MOD_OP: u8 = 0x85;
pub const NOTIFY_OP: u8 = 0x86;
pub const SIZE_OF_OP: u8 = 0x87;
pub const INDEX_OP: u8 = 0x88;
pub const MATCH_OP: u8 = 0x89;
pub const CREATE_DWORD_FIELD_OP: u8 = 0x8A;
pub const CREATE_WORD_FIELD_OP: u8 = 0x8B;
pub const CREATE_BYTE_FIELD_OP: u8 = 0x8C;
pub const CREATE_BIT_FIELD_OP: u8 = 0x8D;
pub const OBJECT_TYPE_OP: u8 = 0x8E;
pub const CREATE_QWORD_FIELD_OP: u8 = 0x8F;
pub const LAND_OP: u8 = 0x90;
pub const LOR_OP: u8 = 0x91;
pub const LNOT_OP: u8 = 0x92;
pub const LEQUAL_OP: u8 = 0x93;
pub const LGREATER_OP: u8 = 0x94;
pub const LLESS_OP: u8 = 0x95;
pub const TO_BUFFER_OP: u8 = 0x96;
pub const TO_DECIMAL_STRING_OP: u8 = 0x97;
pub const TO_HEX_STRING_OP: u8 = 0x98;
pub const TO_INTEGER_OP: u8 = 0x99;
pub const TO_STRING_OP: u8 = 0x9C;
pub const COPY_OBJECT_OP: u8 = 0x9D;
pub const MID_OP: u8 = 0x9E;
pub const CONTINUE_OP: u8 = 0x9F;
pub const IF_OP: u8 = 0xA0;
pub const ELSE_OP: u8 = 0xA1;
pub const WHILE_OP: u8 = 0xA2;
pub const NOOP_OP: u8 = 0xA3;
pub const RETURN_OP: u8 = 0xA4;
pub const BREAK_OP: u8 = 0xA5;
pub const BREAK_POINT_OP: u8 = 0xCC;
pub const ONES_OP: u8 = 0xFF;

pub mod ext {
    pub const MUTEX_OP: u8 = 0x01;
    pub const EVENT_OP: u8 = 0x02;
    pub const COND_REF_OF_OP: u8 = 0x12;
    pub const CREATE_FIELD_OP: u8 = 0x13;
    pub const LOAD_TABLE_OP: u8 = 0x1F;
    pub const LOAD_OP: u8 = 0x20;
    pub const STALL_OP: u8 = 0x21;
    pub const SLEEP_OP: u8 = 0x22;
    pub const ACQUIRE_OP: u8 = 0x23;
    pub const SIGNAL_OP: u8 = 0x24;
    pub const WAIT_OP: u8 = 0x25;
    pub const RESET_OP: u8 = 0x26;
    pub const RELEASE_OP: u8 = 0x27;
    pub const FROM_BCD_OP: u8 = 0x28;
    pub const REVISION_OP: u8 = 0x30;
    pub const DEBUG_OP: u8 = 0x31;
    pub const FATAL_OP: u8 = 0x32;
    pub const TIMER_OP: u8 = 0x33;
    pub const OP_REGION_OP: u8 = 0x80;
    pub const FIELD_OP: u8 = 0x81;
    pub const DEVICE_OP: u8 = 0x82;
    pub const PROCESSOR_OP: u8 = 0x83;
    pub const POWER_RES_OP: u8 = 0x84;
    pub const THERMAL_ZONE_OP: u8 = 0x85;
    pub const INDEX_FIELD_OP: u8 = 0x86;
    pub const BANK_FIELD_OP: u8 = 0x87;
    pub const DATA_REGION_OP: u8 = 0x88;
}

#[derive(Clone, Copy)]
pub enum Opcode {
    Zero,
    One,

    Byte,
    Word,
    Dword,
    Qword,
    String,

    Alias,
    Name,
    Scope,
    Buffer,
    Package,
    VarPackage,
    Method,
    External,
    Local(u8),
    Arg(u8),
    Store,
    RefOf,
    Add,
    Concat,
    Subtract,
    Increment,
    Decrement,
    Multiply,
    Divide,
    ShiftLeft,
    ShiftRight,
    And,
    Nand,
    Or,
    Nor,
    Xor,
    Not,
    FindSetLeftBit,
    FindSetRightBit,
    DerefOf,
    ConcatRes,
    Mod,
    Notify,
    SizeOf,
    Index,
    Match,
    CreateDwordField,
    CreateWordField,
    CreateByteField,
    CreateBitField,
    ObjectType,
    CreateQwordField,
    Land,
    Lor,
    LNotEqual,
    LLessEqual,
    LGreaterEqual,
    LEqual,
    LGreater,
    LLess,
    ToBuffer,
    ToDecimalString,
    ToHexString,
    ToInteger,
    ToString,
    CopyObject,
    Mid,
    Continue,
    If,
    Else,
    While,
    Noop,
    Return,
    Break,
    BreakPoint,
    Ones,
    // ext
    Mutex,
    Event,
    CondRefOf,
    CreateField,
    LoadTable,
    Load,
    Stall,
    Sleep,
    Acquire,
    Signal,
    Wait,
    Reset,
    Release,
    FromBcd,
    Revision,
    Debug,
    Fatal,
    Timer,
    OpRegion,
    Field,
    Device,
    Processor,
    PowerRes,
    ThermalZone,
    IndexField,
    BankField,
    DataRegion,
}

impl Opcode {
    pub fn from(bytecode: &mut Bytecode) -> Option<Self> {
        match bytecode.next()? {
            prefix::EXT_OP_PREFIX => {
                let ext_byte = bytecode.next()?;
                Opcode::from_ext(ext_byte)
            }
            prefix::BYTE_PREFIX => Some(Opcode::Byte),
            prefix::WORD_PREFIX => Some(Opcode::Word),
            prefix::DWORD_PREFIX => Some(Opcode::Dword),
            prefix::QWORD_PREFIX => Some(Opcode::Qword),
            prefix::STRING_PREFIX => Some(Opcode::String),
            ZERO_OP => Some(Opcode::Zero),
            ONE_OP => Some(Opcode::One),
            ALIAS_OP => Some(Opcode::Alias),
            NAME_OP => Some(Opcode::Name),
            SCOPE_OP => Some(Opcode::Scope),
            BUFFER_OP => Some(Opcode::Buffer),
            PACKAGE_OP => Some(Opcode::Package),
            VAR_PACKAGE_OP => Some(Opcode::VarPackage),
            METHOD_OP => Some(Opcode::Method),
            EXTERNAL_OP => Some(Opcode::External),
            LOCAL0_OP => Some(Opcode::Local(0)),
            LOCAL1_OP => Some(Opcode::Local(1)),
            LOCAL2_OP => Some(Opcode::Local(2)),
            LOCAL3_OP => Some(Opcode::Local(3)),
            LOCAL4_OP => Some(Opcode::Local(4)),
            LOCAL5_OP => Some(Opcode::Local(5)),
            LOCAL6_OP => Some(Opcode::Local(6)),
            LOCAL7_OP => Some(Opcode::Local(7)),
            ARG0_OP => Some(Opcode::Arg(0)),
            ARG1_OP => Some(Opcode::Arg(1)),
            ARG2_OP => Some(Opcode::Arg(2)),
            ARG3_OP => Some(Opcode::Arg(3)),
            ARG4_OP => Some(Opcode::Arg(4)),
            ARG5_OP => Some(Opcode::Arg(5)),
            ARG6_OP => Some(Opcode::Arg(6)),
            STORE_OP => Some(Opcode::Store),
            REF_OF_OP => Some(Opcode::RefOf),
            ADD_OP => Some(Opcode::Add),
            CONCAT_OP => Some(Opcode::Concat),
            SUBTRACT_OP => Some(Opcode::Subtract),
            INCREMENT_OP => Some(Opcode::Increment),
            DECREMENT_OP => Some(Opcode::Decrement),
            MULTIPLY_OP => Some(Opcode::Multiply),
            DIVIDE_OP => Some(Opcode::Divide),
            SHIFT_LEFT_OP => Some(Opcode::ShiftLeft),
            SHIFT_RIGHT_OP => Some(Opcode::ShiftRight),
            AND_OP => Some(Opcode::And),
            NAND_OP => Some(Opcode::Nand),
            OR_OP => Some(Opcode::Or),
            NOR_OP => Some(Opcode::Nor),
            XOR_OP => Some(Opcode::Xor),
            NOT_OP => Some(Opcode::Not),
            FIND_SET_LEFT_BIT_OP => Some(Opcode::FindSetLeftBit),
            FIND_SET_RIGHT_BIT_OP => Some(Opcode::FindSetRightBit),
            DEREF_OF_OP => Some(Opcode::DerefOf),
            CONCAT_RES_OP => Some(Opcode::ConcatRes),
            MOD_OP => Some(Opcode::Mod),
            NOTIFY_OP => Some(Opcode::Notify),
            SIZE_OF_OP => Some(Opcode::SizeOf),
            INDEX_OP => Some(Opcode::Index),
            MATCH_OP => Some(Opcode::Match),
            CREATE_DWORD_FIELD_OP => Some(Opcode::CreateDwordField),
            CREATE_WORD_FIELD_OP => Some(Opcode::CreateWordField),
            CREATE_BYTE_FIELD_OP => Some(Opcode::CreateByteField),
            CREATE_BIT_FIELD_OP => Some(Opcode::CreateBitField),
            OBJECT_TYPE_OP => Some(Opcode::ObjectType),
            CREATE_QWORD_FIELD_OP => Some(Opcode::CreateQwordField),
            LAND_OP => Some(Opcode::Land),
            LOR_OP => Some(Opcode::Lor),
            LNOT_OP => Self::not_op(bytecode.next()?),
            LEQUAL_OP => Some(Opcode::LEqual),
            LGREATER_OP => Some(Opcode::LGreater),
            LLESS_OP => Some(Opcode::LLess),
            TO_BUFFER_OP => Some(Opcode::ToBuffer),
            TO_DECIMAL_STRING_OP => Some(Opcode::ToDecimalString),
            TO_HEX_STRING_OP => Some(Opcode::ToHexString),
            TO_INTEGER_OP => Some(Opcode::ToInteger),
            TO_STRING_OP => Some(Opcode::ToString),
            COPY_OBJECT_OP => Some(Opcode::CopyObject),
            MID_OP => Some(Opcode::Mid),
            CONTINUE_OP => Some(Opcode::Continue),
            IF_OP => Some(Opcode::If),
            ELSE_OP => Some(Opcode::Else),
            WHILE_OP => Some(Opcode::While),
            NOOP_OP => Some(Opcode::Noop),
            RETURN_OP => Some(Opcode::Return),
            BREAK_OP => Some(Opcode::Break),
            BREAK_POINT_OP => Some(Opcode::BreakPoint),
            ONES_OP => Some(Opcode::Ones),
            _ => None,
        }
    }

    fn not_op(byte: u8) -> Option<Self> {
        match byte {
            LEQUAL_OP => Some(Self::LNotEqual),
            LGREATER_OP => Some(Self::LLessEqual),
            LLESS_OP => Some(Self::LGreaterEqual),
            _ => None,
        }
    }

    fn from_ext(byte: u8) -> Option<Self> {
        match byte {
            ext::MUTEX_OP => Some(Opcode::Mutex),
            ext::EVENT_OP => Some(Opcode::Event),
            ext::COND_REF_OF_OP => Some(Opcode::CondRefOf),
            ext::CREATE_FIELD_OP => Some(Opcode::CreateField),
            ext::LOAD_TABLE_OP => Some(Opcode::LoadTable),
            ext::LOAD_OP => Some(Opcode::Load),
            ext::STALL_OP => Some(Opcode::Stall),
            ext::SLEEP_OP => Some(Opcode::Sleep),
            ext::ACQUIRE_OP => Some(Opcode::Acquire),
            ext::SIGNAL_OP => Some(Opcode::Signal),
            ext::WAIT_OP => Some(Opcode::Wait),
            ext::RESET_OP => Some(Opcode::Reset),
            ext::RELEASE_OP => Some(Opcode::Release),
            ext::FROM_BCD_OP => Some(Opcode::FromBcd),
            ext::REVISION_OP => Some(Opcode::Revision),
            ext::DEBUG_OP => Some(Opcode::Debug),
            ext::FATAL_OP => Some(Opcode::Fatal),
            ext::TIMER_OP => Some(Opcode::Timer),
            ext::OP_REGION_OP => Some(Opcode::OpRegion),
            ext::FIELD_OP => Some(Opcode::Field),
            ext::DEVICE_OP => Some(Opcode::Device),
            ext::PROCESSOR_OP => Some(Opcode::Processor),
            ext::POWER_RES_OP => Some(Opcode::PowerRes),
            ext::THERMAL_ZONE_OP => Some(Opcode::ThermalZone),
            ext::INDEX_FIELD_OP => Some(Opcode::IndexField),
            ext::BANK_FIELD_OP => Some(Opcode::BankField),
            ext::DATA_REGION_OP => Some(Opcode::DataRegion),
            _ => None,
        }
    }
}
