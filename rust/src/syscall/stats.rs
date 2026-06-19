use super::consts::{STATS_RING_SIZE, SYSCALL_STATS_MAX_ID};

static mut TOTAL: u64 = 0;
static mut ID_COUNT: [u64; (SYSCALL_STATS_MAX_ID as usize) + 1] = [0; (SYSCALL_STATS_MAX_ID as usize) + 1];
static mut RECENT_ID_COUNT: [u64; (SYSCALL_STATS_MAX_ID as usize) + 1] = [0; (SYSCALL_STATS_MAX_ID as usize) + 1];
static mut RECENT_RING: [u16; STATS_RING_SIZE] = [0; STATS_RING_SIZE];
static mut RECENT_HEAD: u32 = 0;
static mut RECENT_SIZE: u32 = 0;

pub fn reset() {
    unsafe {
        TOTAL = 0;
        ID_COUNT = [0; (SYSCALL_STATS_MAX_ID as usize) + 1];
        RECENT_ID_COUNT = [0; (SYSCALL_STATS_MAX_ID as usize) + 1];
        RECENT_RING = [0; STATS_RING_SIZE];
        RECENT_HEAD = 0;
        RECENT_SIZE = 0;
    }
}

pub fn record(id: u64) {
    unsafe {
        let mut ring_id = 0xFFFFu16;
        TOTAL = TOTAL.wrapping_add(1);

        if id <= SYSCALL_STATS_MAX_ID {
            ID_COUNT[id as usize] = ID_COUNT[id as usize].wrapping_add(1);
        }
        if id <= 0xFFFF {
            ring_id = id as u16;
        }

        if RECENT_SIZE as usize >= STATS_RING_SIZE {
            let old_id = RECENT_RING[RECENT_HEAD as usize] as u64;
            if old_id <= SYSCALL_STATS_MAX_ID && RECENT_ID_COUNT[old_id as usize] > 0 {
                RECENT_ID_COUNT[old_id as usize] -= 1;
            }
        } else {
            RECENT_SIZE += 1;
        }

        RECENT_RING[RECENT_HEAD as usize] = ring_id;
        if id <= SYSCALL_STATS_MAX_ID {
            RECENT_ID_COUNT[id as usize] = RECENT_ID_COUNT[id as usize].wrapping_add(1);
        }

        RECENT_HEAD += 1;
        if RECENT_HEAD as usize >= STATS_RING_SIZE {
            RECENT_HEAD = 0;
        }
    }
}

pub fn total_count() -> u64 {
    unsafe { TOTAL }
}

pub fn id_count(id: u64) -> u64 {
    if id > SYSCALL_STATS_MAX_ID {
        return 0;
    }
    unsafe { ID_COUNT[id as usize] }
}

pub fn recent_window() -> u64 {
    unsafe { RECENT_SIZE as u64 }
}

pub fn recent_id(id: u64) -> u64 {
    if id > SYSCALL_STATS_MAX_ID {
        return 0;
    }
    unsafe { RECENT_ID_COUNT[id as usize] }
}
