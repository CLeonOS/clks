#include <clks/pty.h>
#include <clks/string.h>
#include <clks/types.h>

#define CLKS_PTY_MAX 16U
#define CLKS_PTY_BUFFER_SIZE 8192U

struct clks_pty_slot {
    clks_bool used;
    u64 id;
    u64 ref_count;
    u8 buffer[CLKS_PTY_BUFFER_SIZE];
    u64 read_pos;
    u64 write_pos;
    u64 count;
};

static struct clks_pty_slot clks_pty_slots[CLKS_PTY_MAX];
static u64 clks_pty_next_id = 1ULL;

static i32 clks_pty_find_slot(u64 pty_id) {
    u32 i;

    if (pty_id == 0ULL) {
        return -1;
    }

    for (i = 0U; i < CLKS_PTY_MAX; i++) {
        if (clks_pty_slots[i].used == CLKS_TRUE && clks_pty_slots[i].id == pty_id) {
            return (i32)i;
        }
    }

    return -1;
}

void clks_pty_init(void) {
    clks_memset(clks_pty_slots, 0, sizeof(clks_pty_slots));
    clks_pty_next_id = 1ULL;
}

u64 clks_pty_alloc(void) {
    u32 i;

    for (i = 0U; i < CLKS_PTY_MAX; i++) {
        if (clks_pty_slots[i].used == CLKS_FALSE) {
            u64 id = clks_pty_next_id;

            clks_pty_next_id++;
            if (clks_pty_next_id == 0ULL) {
                clks_pty_next_id = 1ULL;
            }
            if (id == 0ULL) {
                id = clks_pty_next_id;
                clks_pty_next_id++;
            }

            clks_memset(&clks_pty_slots[i], 0, sizeof(clks_pty_slots[i]));
            clks_pty_slots[i].used = CLKS_TRUE;
            clks_pty_slots[i].id = id;
            clks_pty_slots[i].ref_count = 1ULL;
            return id;
        }
    }

    return 0ULL;
}

clks_bool clks_pty_retain(u64 pty_id) {
    i32 slot = clks_pty_find_slot(pty_id);

    if (slot < 0) {
        return CLKS_FALSE;
    }

    if (clks_pty_slots[(u32)slot].ref_count == 0xFFFFFFFFFFFFFFFFULL) {
        return CLKS_FALSE;
    }

    clks_pty_slots[(u32)slot].ref_count++;
    return CLKS_TRUE;
}

void clks_pty_release(u64 pty_id) {
    i32 slot = clks_pty_find_slot(pty_id);

    if (slot < 0) {
        return;
    }

    if (clks_pty_slots[(u32)slot].ref_count > 1ULL) {
        clks_pty_slots[(u32)slot].ref_count--;
        return;
    }

    clks_memset(&clks_pty_slots[(u32)slot], 0, sizeof(clks_pty_slots[(u32)slot]));
}

u64 clks_pty_read(u64 pty_id, void *out_buffer, u64 size) {
    i32 slot_index;
    struct clks_pty_slot *slot;
    u8 *dst;
    u64 read_count = 0ULL;

    if (out_buffer == CLKS_NULL && size != 0ULL) {
        return (u64)-1;
    }

    if (size == 0ULL) {
        return 0ULL;
    }

    slot_index = clks_pty_find_slot(pty_id);
    if (slot_index < 0) {
        return (u64)-1;
    }

    slot = &clks_pty_slots[(u32)slot_index];
    dst = (u8 *)out_buffer;

    while (read_count < size && slot->count > 0ULL) {
        dst[(usize)read_count] = slot->buffer[(usize)slot->read_pos];
        slot->read_pos = (slot->read_pos + 1ULL) % (u64)CLKS_PTY_BUFFER_SIZE;
        slot->count--;
        read_count++;
    }

    return read_count;
}

u64 clks_pty_write(u64 pty_id, const void *buffer, u64 size) {
    i32 slot_index;
    struct clks_pty_slot *slot;
    const u8 *src;
    u64 i;

    if (buffer == CLKS_NULL && size != 0ULL) {
        return (u64)-1;
    }

    if (size == 0ULL) {
        return 0ULL;
    }

    slot_index = clks_pty_find_slot(pty_id);
    if (slot_index < 0) {
        return (u64)-1;
    }

    slot = &clks_pty_slots[(u32)slot_index];
    src = (const u8 *)buffer;

    for (i = 0ULL; i < size; i++) {
        if (slot->count == (u64)CLKS_PTY_BUFFER_SIZE) {
            slot->read_pos = (slot->read_pos + 1ULL) % (u64)CLKS_PTY_BUFFER_SIZE;
            slot->count--;
        }

        slot->buffer[(usize)slot->write_pos] = src[(usize)i];
        slot->write_pos = (slot->write_pos + 1ULL) % (u64)CLKS_PTY_BUFFER_SIZE;
        slot->count++;
    }

    return size;
}
