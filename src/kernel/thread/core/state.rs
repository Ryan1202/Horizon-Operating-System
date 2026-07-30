#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ThreadState {
    New,
    Registered,
    Idle,
    Ready,
    Running,
    Blocking,
    Blocked,
    Waking,
    Dead,
}
