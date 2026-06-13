#include <clks/boot.h>
#include <clks/cpu.h>
#include <clks/disk.h>
#include <clks/elf64.h>
#include <clks/exec.h>
#include <clks/framebuffer.h>
#include <clks/fs.h>
#include <clks/heap.h>
#include <clks/interrupts.h>
#include <clks/keyboard.h>
#include <clks/log.h>
#include <clks/mouse.h>
#include <clks/net.h>
#include <clks/pmm.h>
#include <clks/pty.h>
#include <clks/rust.h>
#include <clks/scheduler.h>
#include <clks/serial.h>
#include <clks/string.h>
#include <clks/types.h>
#include <clks/tty.h>
#include <clks/user.h>
#include <clks/vm.h>

/* Process runtime core: this is where tiny mistakes become giant explosions. */

typedef u64 (*clks_exec_entry_fn)(void);

#define CLKS_EXEC_RUN_STACK_BYTES (1024ULL * 1024ULL)
#define CLKS_EXEC_RUN_STACK_TIER_1 (768ULL * 1024ULL)
#define CLKS_EXEC_RUN_STACK_TIER_2 (512ULL * 1024ULL)
#define CLKS_EXEC_RUN_STACK_TIER_3 (256ULL * 1024ULL)
#define CLKS_EXEC_RUN_STACK_TIER_4 (128ULL * 1024ULL)
#define CLKS_EXEC_RUN_STACK_TIER_5 (64ULL * 1024ULL)
#define CLKS_EXEC_MAX_PROCS 64U
/* 64 is a compromise: enough to be useful, small enough to not eat RAM for breakfast. */
#define CLKS_EXEC_MAX_DEPTH 16U
#define CLKS_EXEC_PATH_MAX 192U
#define CLKS_EXEC_ARG_LINE_MAX 256U
#define CLKS_EXEC_ENV_LINE_MAX 512U
#define CLKS_EXEC_FD_MAX 32U
#define CLKS_EXEC_MAX_ARGS 24U
#define CLKS_EXEC_MAX_ENVS 24U
#define CLKS_EXEC_ITEM_MAX 128U
#define CLKS_EXEC_STATUS_SIGNAL_FLAG (1ULL << 63)
#define CLKS_EXEC_DEFAULT_KILL_SIGNAL CLKS_EXEC_SIGNAL_TERM
#define CLKS_EXEC_KERNEL_ADDR_BASE 0xFFFF800000000000ULL
#define CLKS_EXEC_UNWIND_CTX_BYTES 64ULL
#define CLKS_EXEC_FD_INHERIT ((u64) - 1)
#define CLKS_EXEC_DYNLIB_MAX 32U
#define CLKS_EXEC_INTERNAL_SUSPEND_STATUS 0x434C4B5353555350ULL
#define CLKS_EXEC_USER_VM_AREA_MAX 128U
#define CLKS_EXEC_USER_VM_ALLOC_MAX (32ULL * 1024ULL * 1024ULL)
#define CLKS_EXEC_USER_VM_ALIGN 4096ULL
#define CLKS_EXEC_USER_VM_BASE 0x0000004000000000ULL
#define CLKS_EXEC_USER_VM_LIMIT 0x0000007000000000ULL
#define CLKS_EXEC_MMAP_FLAG_SHARED 0x01ULL
#define CLKS_EXEC_MMAP_FLAG_PRIVATE 0x02ULL
#define CLKS_EXEC_MMAP_FLAG_FIXED 0x10ULL
#define CLKS_EXEC_MMAP_FLAG_ANONYMOUS 0x20ULL
#define CLKS_EXEC_USER_NAME_MAX 32U
#define CLKS_EXEC_USER_HOME_MAX 96U
#define CLKS_EXEC_USER_ROLE_USER 0ULL
#define CLKS_EXEC_USER_ROLE_ADMIN 1ULL
#define CLKS_EXEC_USER_UID_NOBODY 65534ULL
#define CLKS_EXEC_SCHED_TASK_INVALID 0xFFFFFFFFULL

#ifndef CLKS_CFG_EXEC_SERIAL_LOG
#define CLKS_CFG_EXEC_SERIAL_LOG 1
#endif

#ifndef CLKS_CFG_EXEC_CONTEXT_SWITCH_LOG
#define CLKS_CFG_EXEC_CONTEXT_SWITCH_LOG 0
#endif

#define CLKS_EXEC_ELF64_MAGIC_0 0x7FU
#define CLKS_EXEC_ELF64_MAGIC_1 'E'
#define CLKS_EXEC_ELF64_MAGIC_2 'L'
#define CLKS_EXEC_ELF64_MAGIC_3 'F'
#define CLKS_EXEC_ELF64_CLASS_64 2U
#define CLKS_EXEC_ELF64_DATA_LSB 1U
#define CLKS_EXEC_ELF64_VERSION 1U

#define CLKS_EXEC_ELF64_SHT_SYMTAB 2U
#define CLKS_EXEC_ELF64_SHT_DYNSYM 11U
#define CLKS_EXEC_ELF64_SHN_UNDEF 0U
#define CLKS_EXEC_ELF64_STT_NOTYPE 0U
#define CLKS_EXEC_ELF64_STT_FUNC 2U

enum clks_exec_fd_kind {
    CLKS_EXEC_FD_KIND_NONE = 0,
    CLKS_EXEC_FD_KIND_TTY = 1,
    CLKS_EXEC_FD_KIND_FILE = 2,
    CLKS_EXEC_FD_KIND_DEV_NULL = 3,
    CLKS_EXEC_FD_KIND_DEV_ZERO = 4,
    CLKS_EXEC_FD_KIND_DEV_RANDOM = 5,
    CLKS_EXEC_FD_KIND_PTY = 6,
    CLKS_EXEC_FD_KIND_DEV_FB0 = 7,
    CLKS_EXEC_FD_KIND_DEV_KBD = 8,
    CLKS_EXEC_FD_KIND_DEV_MOUSE = 9,
    CLKS_EXEC_FD_KIND_DEV_NET0 = 10,
    CLKS_EXEC_FD_KIND_DEV_TTY0 = 11,
    CLKS_EXEC_FD_KIND_DEV_DISK0 = 12,
};

enum clks_exec_proc_state {
    CLKS_EXEC_PROC_UNUSED = 0,
    CLKS_EXEC_PROC_PENDING = 1,
    CLKS_EXEC_PROC_RUNNING = 2,
    CLKS_EXEC_PROC_EXITED = 3,
    CLKS_EXEC_PROC_STOPPED = 4,
};

enum clks_exec_thread_state {
    CLKS_EXEC_THREAD_NONE = 0,
    CLKS_EXEC_THREAD_READY = 1,
    CLKS_EXEC_THREAD_RUNNING = 2,
    CLKS_EXEC_THREAD_BLOCKED = 3,
    CLKS_EXEC_THREAD_SLEEPING = 4,
    CLKS_EXEC_THREAD_STOPPED = 5,
    CLKS_EXEC_THREAD_ZOMBIE = 6,
};

enum clks_exec_block_reason {
    CLKS_EXEC_BLOCK_REASON_NONE = 0,
    CLKS_EXEC_BLOCK_REASON_TTY_INPUT = 1,
    CLKS_EXEC_BLOCK_REASON_PTY_INPUT = 2,
    CLKS_EXEC_BLOCK_REASON_SLEEP = 3,
    CLKS_EXEC_BLOCK_REASON_WAIT_CHILD = 4,
    CLKS_EXEC_BLOCK_REASON_YIELD = 5,
    CLKS_EXEC_BLOCK_REASON_IO = 6,
};

struct clks_exec_saved_interrupt_frame {
    u64 rax;
    u64 rbx;
    u64 rcx;
    u64 rdx;
    u64 rsi;
    u64 rdi;
    u64 rbp;
    u64 r8;
    u64 r9;
    u64 r10;
    u64 r11;
    u64 r12;
    u64 r13;
    u64 r14;
    u64 r15;
    u64 vector;
    u64 error_code;
    u64 rip;
    u64 cs;
    u64 rflags;
    u64 rsp;
    u64 ss;
};

struct clks_exec_fd_entry {
    clks_bool used;
    enum clks_exec_fd_kind kind;
    u64 flags;
    u64 offset;
    u32 tty_index;
    u64 pty_id;
    char path[CLKS_EXEC_PATH_MAX];
};

struct clks_exec_user_vm_area {
    void *ptr;
    u64 size;
    u64 requested_size;
    u64 flags;
    u32 page_count;
};

struct clks_exec_proc_record {
    clks_bool used;
    enum clks_exec_proc_state state;
    u64 pid;
    u64 ppid;
    u64 main_thread_id;
    enum clks_exec_thread_state thread_state;
    u64 scheduler_task_id;
    enum clks_exec_block_reason blocked_reason;
    u64 wake_tick;
    u64 wait_target_pid;
    clks_bool parent_waiting;
    u64 started_tick;
    u64 exited_tick;
    u64 run_started_tick;
    u64 runtime_ticks_accum;
    u64 exit_status;
    u32 tty_index;
    u64 image_mem_bytes;
    char path[CLKS_EXEC_PATH_MAX];
    char argv_line[CLKS_EXEC_ARG_LINE_MAX];
    char env_line[CLKS_EXEC_ENV_LINE_MAX];
    u32 argc;
    u32 envc;
    char argv_items[CLKS_EXEC_MAX_ARGS][CLKS_EXEC_ITEM_MAX];
    char env_items[CLKS_EXEC_MAX_ENVS][CLKS_EXEC_ITEM_MAX];
    u64 last_signal;
    u64 last_fault_vector;
    u64 last_fault_error;
    u64 last_fault_rip;
    struct clks_exec_fd_entry fds[CLKS_EXEC_FD_MAX];
    clks_bool loaded_active;
    struct clks_elf64_loaded_image loaded;
    void *run_stack_base;
    u64 run_stack_bytes;
    u64 address_space_cr3;
    u64 user_vm_bytes;
    struct clks_exec_user_vm_area user_vm_areas[CLKS_EXEC_USER_VM_AREA_MAX];
    u64 uid;
    u64 user_role;
    clks_bool user_logged_in;
    clks_bool disk_login_required;
    char username[CLKS_EXEC_USER_NAME_MAX];
    char user_home[CLKS_EXEC_USER_HOME_MAX];
    clks_bool saved_frame_valid;
    struct clks_exec_saved_interrupt_frame saved_frame;
};

struct clks_exec_elf64_ehdr {
    u8 e_ident[16];
    u16 e_type;
    u16 e_machine;
    u32 e_version;
    u64 e_entry;
    u64 e_phoff;
    u64 e_shoff;
    u32 e_flags;
    u16 e_ehsize;
    u16 e_phentsize;
    u16 e_phnum;
    u16 e_shentsize;
    u16 e_shnum;
    u16 e_shstrndx;
};

struct clks_exec_elf64_shdr {
    u32 sh_name;
    u32 sh_type;
    u64 sh_flags;
    u64 sh_addr;
    u64 sh_offset;
    u64 sh_size;
    u32 sh_link;
    u32 sh_info;
    u64 sh_addralign;
    u64 sh_entsize;
};

struct clks_exec_elf64_sym {
    u32 st_name;
    u8 st_info;
    u8 st_other;
    u16 st_shndx;
    u64 st_value;
    u64 st_size;
};

struct clks_exec_dynlib_slot {
    clks_bool used;
    u64 handle;
    u64 owner_pid;
    u64 ref_count;
    char path[CLKS_EXEC_PATH_MAX];
    const void *file_image;
    u64 file_size;
    struct clks_elf64_loaded_image loaded;
};

#if defined(CLKS_ARCH_X86_64)
extern u64 clks_exec_call_on_stack_x86_64(void *entry_ptr, void *stack_top);
extern void clks_exec_abort_to_caller_x86_64(void);
extern u64 clks_exec_resume_frame_x86_64(void *frame_ptr, void *unwind_slot);
static u64 clks_exec_read_cr2(void) {
    u64 value = 0ULL;
    __asm__ volatile("mov %%cr2, %0" : "=r"(value));
    return value;
}
#endif

static u64 clks_exec_requests = 0ULL;
static u64 clks_exec_success = 0ULL;
static u32 clks_exec_running_depth = 0U;
static clks_bool clks_exec_pending_dispatch_active = CLKS_FALSE;
static u32 clks_exec_dispatch_cursor = 0U;
static u64 clks_exec_random_state = 0xA5A55A5A12345678ULL;

static struct clks_exec_proc_record clks_exec_proc_table[CLKS_EXEC_MAX_PROCS];
static u64 clks_exec_next_pid = 1ULL;
static u64 clks_exec_next_vm_base = CLKS_EXEC_USER_VM_BASE;
static u64 clks_exec_pid_stack[CLKS_EXEC_MAX_DEPTH];
static clks_bool clks_exec_exit_requested_stack[CLKS_EXEC_MAX_DEPTH];
static u64 clks_exec_exit_status_stack[CLKS_EXEC_MAX_DEPTH];
static clks_bool clks_exec_stop_requested_stack[CLKS_EXEC_MAX_DEPTH];
static u64 clks_exec_unwind_slot_stack[CLKS_EXEC_MAX_DEPTH];
static clks_bool clks_exec_unwind_slot_valid_stack[CLKS_EXEC_MAX_DEPTH];
static u64 clks_exec_image_begin_stack[CLKS_EXEC_MAX_DEPTH];
static u64 clks_exec_image_end_stack[CLKS_EXEC_MAX_DEPTH];
static u64 clks_exec_image_vaddr_begin_stack[CLKS_EXEC_MAX_DEPTH];
static u64 clks_exec_image_vaddr_end_stack[CLKS_EXEC_MAX_DEPTH];
static u64 clks_exec_stack_begin_stack[CLKS_EXEC_MAX_DEPTH];
static u64 clks_exec_stack_end_stack[CLKS_EXEC_MAX_DEPTH];
static u32 clks_exec_pid_stack_depth = 0U;
static struct clks_exec_dynlib_slot clks_exec_dynlib_table[CLKS_EXEC_DYNLIB_MAX];
static u64 clks_exec_next_dynlib_handle = 1ULL;

static struct clks_exec_proc_record *clks_exec_current_proc(void);
static clks_bool clks_exec_proc_bind_scheduler_task(struct clks_exec_proc_record *proc);
static enum clks_task_state clks_exec_proc_to_task_state(enum clks_exec_proc_state proc_state,
                                                         enum clks_exec_thread_state thread_state,
                                                         enum clks_exec_block_reason blocked_reason);
static void clks_exec_proc_sync_scheduler_state(struct clks_exec_proc_record *proc);
static void clks_exec_proc_set_state(struct clks_exec_proc_record *proc, enum clks_exec_proc_state proc_state,
                                     enum clks_exec_thread_state thread_state,
                                     enum clks_exec_block_reason blocked_reason);
static void clks_exec_proc_set_block(struct clks_exec_proc_record *proc,
                                     enum clks_exec_block_reason blocked_reason, u64 wake_tick,
                                     u64 wait_target_pid);
static void clks_exec_proc_clear_block(struct clks_exec_proc_record *proc);
static u64 clks_exec_proc_address_space(const struct clks_exec_proc_record *proc);
static void clks_exec_switch_address_space(u64 target_cr3, u64 *out_prev_cr3, clks_bool *out_switched);
static void clks_exec_restore_address_space(u64 prev_cr3, clks_bool switched);

