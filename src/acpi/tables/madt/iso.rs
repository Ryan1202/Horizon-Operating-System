#[repr(C, packed)]
pub struct InterruptSourceOverride {
    _type: u8,
    length: u8,
    bus: u8,
    pub source: u8,
    pub gsi: u32,
    pub flags: MpsIntiFlags,
}

#[repr(transparent)]
pub struct MpsIntiFlags(u16);

impl MpsIntiFlags {
    pub const fn active_low(&self) -> Option<bool> {
        let polarity = self.0 & 0b11;
        if polarity == 3 {
            Some(true)
        } else if polarity == 1 {
            Some(false)
        } else {
            None
        }
    }

    pub const fn level_triggered(&self) -> Option<bool> {
        let trigger_mode = (self.0 >> 2) & 0b11;
        if trigger_mode == 3 {
            Some(true)
        } else if trigger_mode == 1 {
            Some(false)
        } else {
            None
        }
    }
}
