#ifndef CLKS_EXEC_H
#define CLKS_EXEC_H

#include <clks/types.h>

void clks_exec_init(void);
clks_bool clks_exec_run_path(const char *path, u64 *out_status);
clks_bool clks_exec_run_pathv(const char *path, const char *argv_line, const char *env_line, u64 *out_status);
clks_bool clks_exec_spawn_path(const char *path, u64 *out_pid);
clks_bool clks_exec_spawn_pathv(const char *path, const char *argv_line, const char *env_line, u64 *out_pid);
u64 clks_exec_wait_pid(u64 pid, u64 *out_status);
clks_bool clks_exec_request_exit(u64 status);
u64 clks_exec_current_pid(void);
u32 clks_exec_current_tty(void);
u64 clks_exec_current_argc(void);
clks_bool clks_exec_copy_current_argv(u64 index, char *out_value, usize out_size);
u64 clks_exec_current_envc(void);
clks_bool clks_exec_copy_current_env(u64 index, char *out_value, usize out_size);
u64 clks_exec_current_signal(void);
u64 clks_exec_current_fault_vector(void);
u64 clks_exec_current_fault_error(void);
u64 clks_exec_current_fault_rip(void);
clks_bool clks_exec_handle_exception(u64 vector,
                                     u64 error_code,
                                     u64 rip,
                                     u64 *io_rip,
                                     u64 *io_rdi,
                                     u64 *io_rsi);
u64 clks_exec_sleep_ticks(u64 ticks);
u64 clks_exec_yield(void);
void clks_exec_tick(u64 tick);
u64 clks_exec_request_count(void);
u64 clks_exec_success_count(void);
clks_bool clks_exec_is_running(void);
clks_bool clks_exec_current_path_is_user(void);

#endif
