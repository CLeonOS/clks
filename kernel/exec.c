#include <clks/elf64.h>
#include <clks/exec.h>
#include <clks/fs.h>
#include <clks/log.h>
#include <clks/types.h>

static u64 clks_exec_requests = 0ULL;
static u64 clks_exec_success = 0ULL;

void clks_exec_init(void) {
    clks_exec_requests = 0ULL;
    clks_exec_success = 0ULL;
    clks_log(CLKS_LOG_INFO, "EXEC", "PATH EXEC FRAMEWORK ONLINE");
}

clks_bool clks_exec_run_path(const char *path, u64 *out_status) {
    const void *image;
    u64 image_size = 0ULL;
    struct clks_elf64_info info;

    clks_exec_requests++;

    if (out_status != CLKS_NULL) {
        *out_status = (u64)-1;
    }

    if (path == CLKS_NULL || path[0] != '/') {
        clks_log(CLKS_LOG_WARN, "EXEC", "INVALID EXEC PATH");
        return CLKS_FALSE;
    }

    image = clks_fs_read_all(path, &image_size);

    if (image == CLKS_NULL || image_size == 0ULL) {
        clks_log(CLKS_LOG_WARN, "EXEC", "EXEC FILE MISSING");
        clks_log(CLKS_LOG_WARN, "EXEC", path);
        return CLKS_FALSE;
    }

    if (clks_elf64_inspect(image, image_size, &info) == CLKS_FALSE) {
        clks_log(CLKS_LOG_WARN, "EXEC", "EXEC ELF INVALID");
        clks_log(CLKS_LOG_WARN, "EXEC", path);
        return CLKS_FALSE;
    }

    clks_exec_success++;

    if (out_status != CLKS_NULL) {
        *out_status = 0ULL;
    }

    clks_log(CLKS_LOG_INFO, "EXEC", "EXEC REQUEST ACCEPTED");
    clks_log(CLKS_LOG_INFO, "EXEC", path);
    clks_log_hex(CLKS_LOG_INFO, "EXEC", "ENTRY", info.entry);
    clks_log_hex(CLKS_LOG_INFO, "EXEC", "PHNUM", (u64)info.phnum);

    return CLKS_TRUE;
}

u64 clks_exec_request_count(void) {
    return clks_exec_requests;
}

u64 clks_exec_success_count(void) {
    return clks_exec_success;
}