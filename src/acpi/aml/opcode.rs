use crate::acpi::aml::Bytecode;

pub(super) mod prefix {
    pub const BYTE_PREFIX: u8 = 0x0A;
    pub const WORD_PREFIX: u8 = 0x0B;
    pub const DWORD_PREFIX: u8 = 0x0C;
    pub const STRING_PREFIX: u8 = 0x0D;
    pub const QWORD_PREFIX: u8 = 0x0E;

    pub const DUAL_NAME_PREFIX: u8 = 0x2E;
    pub const MULTI_NAME_PREFIX: u8 = 0x2F;

    pub const EXT_OP_PREFIX: u8 = 0x5B;
}

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
    pub fn parse(bytecode: &mut Bytecode) -> Result<Self, Option<u8>> {
        let byte = bytecode.next().ok_or(None)?;
        match byte {
            prefix::EXT_OP_PREFIX => {
                let ext_byte = bytecode.next().ok_or(None)?;
                Opcode::from_ext(ext_byte)
            }
            prefix::BYTE_PREFIX => Ok(Opcode::Byte),
            prefix::WORD_PREFIX => Ok(Opcode::Word),
            prefix::DWORD_PREFIX => Ok(Opcode::Dword),
            prefix::QWORD_PREFIX => Ok(Opcode::Qword),
            prefix::STRING_PREFIX => Ok(Opcode::String),
            ZERO_OP => Ok(Opcode::Zero),
            ONE_OP => Ok(Opcode::One),
            ALIAS_OP => Ok(Opcode::Alias),
            NAME_OP => Ok(Opcode::Name),
            SCOPE_OP => Ok(Opcode::Scope),
            BUFFER_OP => Ok(Opcode::Buffer),
            PACKAGE_OP => Ok(Opcode::Package),
            VAR_PACKAGE_OP => Ok(Opcode::VarPackage),
            METHOD_OP => Ok(Opcode::Method),
            EXTERNAL_OP => Ok(Opcode::External),
            LOCAL0_OP => Ok(Opcode::Local(0)),
            LOCAL1_OP => Ok(Opcode::Local(1)),
            LOCAL2_OP => Ok(Opcode::Local(2)),
            LOCAL3_OP => Ok(Opcode::Local(3)),
            LOCAL4_OP => Ok(Opcode::Local(4)),
            LOCAL5_OP => Ok(Opcode::Local(5)),
            LOCAL6_OP => Ok(Opcode::Local(6)),
            LOCAL7_OP => Ok(Opcode::Local(7)),
            ARG0_OP => Ok(Opcode::Arg(0)),
            ARG1_OP => Ok(Opcode::Arg(1)),
            ARG2_OP => Ok(Opcode::Arg(2)),
            ARG3_OP => Ok(Opcode::Arg(3)),
            ARG4_OP => Ok(Opcode::Arg(4)),
            ARG5_OP => Ok(Opcode::Arg(5)),
            ARG6_OP => Ok(Opcode::Arg(6)),
            STORE_OP => Ok(Opcode::Store),
            REF_OF_OP => Ok(Opcode::RefOf),
            ADD_OP => Ok(Opcode::Add),
            CONCAT_OP => Ok(Opcode::Concat),
            SUBTRACT_OP => Ok(Opcode::Subtract),
            INCREMENT_OP => Ok(Opcode::Increment),
            DECREMENT_OP => Ok(Opcode::Decrement),
            MULTIPLY_OP => Ok(Opcode::Multiply),
            DIVIDE_OP => Ok(Opcode::Divide),
            SHIFT_LEFT_OP => Ok(Opcode::ShiftLeft),
            SHIFT_RIGHT_OP => Ok(Opcode::ShiftRight),
            AND_OP => Ok(Opcode::And),
            NAND_OP => Ok(Opcode::Nand),
            OR_OP => Ok(Opcode::Or),
            NOR_OP => Ok(Opcode::Nor),
            XOR_OP => Ok(Opcode::Xor),
            NOT_OP => Ok(Opcode::Not),
            FIND_SET_LEFT_BIT_OP => Ok(Opcode::FindSetLeftBit),
            FIND_SET_RIGHT_BIT_OP => Ok(Opcode::FindSetRightBit),
            DEREF_OF_OP => Ok(Opcode::DerefOf),
            CONCAT_RES_OP => Ok(Opcode::ConcatRes),
            MOD_OP => Ok(Opcode::Mod),
            NOTIFY_OP => Ok(Opcode::Notify),
            SIZE_OF_OP => Ok(Opcode::SizeOf),
            INDEX_OP => Ok(Opcode::Index),
            MATCH_OP => Ok(Opcode::Match),
            CREATE_DWORD_FIELD_OP => Ok(Opcode::CreateDwordField),
            CREATE_WORD_FIELD_OP => Ok(Opcode::CreateWordField),
            CREATE_BYTE_FIELD_OP => Ok(Opcode::CreateByteField),
            CREATE_BIT_FIELD_OP => Ok(Opcode::CreateBitField),
            OBJECT_TYPE_OP => Ok(Opcode::ObjectType),
            CREATE_QWORD_FIELD_OP => Ok(Opcode::CreateQwordField),
            LAND_OP => Ok(Opcode::Land),
            LOR_OP => Ok(Opcode::Lor),
            LNOT_OP => Self::not_op(bytecode.next().ok_or(None)?),
            LEQUAL_OP => Ok(Opcode::LEqual),
            LGREATER_OP => Ok(Opcode::LGreater),
            LLESS_OP => Ok(Opcode::LLess),
            TO_BUFFER_OP => Ok(Opcode::ToBuffer),
            TO_DECIMAL_STRING_OP => Ok(Opcode::ToDecimalString),
            TO_HEX_STRING_OP => Ok(Opcode::ToHexString),
            TO_INTEGER_OP => Ok(Opcode::ToInteger),
            TO_STRING_OP => Ok(Opcode::ToString),
            COPY_OBJECT_OP => Ok(Opcode::CopyObject),
            MID_OP => Ok(Opcode::Mid),
            CONTINUE_OP => Ok(Opcode::Continue),
            IF_OP => Ok(Opcode::If),
            ELSE_OP => Ok(Opcode::Else),
            WHILE_OP => Ok(Opcode::While),
            NOOP_OP => Ok(Opcode::Noop),
            RETURN_OP => Ok(Opcode::Return),
            BREAK_OP => Ok(Opcode::Break),
            BREAK_POINT_OP => Ok(Opcode::BreakPoint),
            ONES_OP => Ok(Opcode::Ones),
            _ => Err(Some(byte)),
        }
    }

    fn not_op(byte: u8) -> Result<Self, Option<u8>> {
        match byte {
            LEQUAL_OP => Ok(Self::LNotEqual),
            LGREATER_OP => Ok(Self::LLessEqual),
            LLESS_OP => Ok(Self::LGreaterEqual),
            _ => Err(None),
        }
    }

    fn from_ext(byte: u8) -> Result<Self, Option<u8>> {
        match byte {
            ext::MUTEX_OP => Ok(Opcode::Mutex),
            ext::EVENT_OP => Ok(Opcode::Event),
            ext::COND_REF_OF_OP => Ok(Opcode::CondRefOf),
            ext::CREATE_FIELD_OP => Ok(Opcode::CreateField),
            ext::LOAD_TABLE_OP => Ok(Opcode::LoadTable),
            ext::LOAD_OP => Ok(Opcode::Load),
            ext::STALL_OP => Ok(Opcode::Stall),
            ext::SLEEP_OP => Ok(Opcode::Sleep),
            ext::ACQUIRE_OP => Ok(Opcode::Acquire),
            ext::SIGNAL_OP => Ok(Opcode::Signal),
            ext::WAIT_OP => Ok(Opcode::Wait),
            ext::RESET_OP => Ok(Opcode::Reset),
            ext::RELEASE_OP => Ok(Opcode::Release),
            ext::FROM_BCD_OP => Ok(Opcode::FromBcd),
            ext::REVISION_OP => Ok(Opcode::Revision),
            ext::DEBUG_OP => Ok(Opcode::Debug),
            ext::FATAL_OP => Ok(Opcode::Fatal),
            ext::TIMER_OP => Ok(Opcode::Timer),
            ext::OP_REGION_OP => Ok(Opcode::OpRegion),
            ext::FIELD_OP => Ok(Opcode::Field),
            ext::DEVICE_OP => Ok(Opcode::Device),
            ext::PROCESSOR_OP => Ok(Opcode::Processor),
            ext::POWER_RES_OP => Ok(Opcode::PowerRes),
            ext::THERMAL_ZONE_OP => Ok(Opcode::ThermalZone),
            ext::INDEX_FIELD_OP => Ok(Opcode::IndexField),
            ext::BANK_FIELD_OP => Ok(Opcode::BankField),
            ext::DATA_REGION_OP => Ok(Opcode::DataRegion),
            _ => Err(None),
        }
    }
}
