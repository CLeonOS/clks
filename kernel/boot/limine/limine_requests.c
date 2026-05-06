#include <clks/boot.h>
#include <clks/compiler.h>

CLKS_USED static volatile u64 limine_requests_start[] __attribute__((section(".limine_requests_start"))) =
    LIMINE_REQUESTS_START_MARKER;

CLKS_USED static volatile u64 limine_base_revision[] __attribute__((section(".limine_requests"))) =
    LIMINE_BASE_REVISION(3);

CLKS_USED static volatile struct limine_framebuffer_request limine_framebuffer_request
    __attribute__((section(".limine_requests"))) = {
        .id = LIMINE_FRAMEBUFFER_REQUEST,
        .revision = 0,
        .response = CLKS_NULL,
};

CLKS_USED static volatile struct limine_memmap_request limine_memmap_request
    __attribute__((section(".limine_requests"))) = {
        .id = LIMINE_MEMMAP_REQUEST,
        .revision = 0,
        .response = CLKS_NULL,
};

CLKS_USED static volatile struct limine_executable_file_request limine_executable_file_request
    __attribute__((section(".limine_requests"))) = {
        .id = LIMINE_EXECUTABLE_FILE_REQUEST,
        .revision = 0,
        .response = CLKS_NULL,
};

CLKS_USED static volatile struct limine_module_request limine_module_request
    __attribute__((section(".limine_requests"))) = {
        .id = LIMINE_MODULE_REQUEST,
        .revision = 0,
        .response = CLKS_NULL,
};

CLKS_USED static volatile struct limine_hhdm_request limine_hhdm_request
    __attribute__((section(".limine_requests"))) = {
        .id = LIMINE_HHDM_REQUEST,
        .revision = 0,
        .response = CLKS_NULL,
};

CLKS_USED static volatile u64 limine_requests_end[] __attribute__((section(".limine_requests_end"))) =
    LIMINE_REQUESTS_END_MARKER;

clks_bool clks_boot_base_revision_supported(void) {
    return (limine_base_revision[2] == 0) ? CLKS_TRUE : CLKS_FALSE;
}

const struct limine_framebuffer *clks_boot_get_framebuffer(void) {
    volatile struct limine_framebuffer_request *request = &limine_framebuffer_request;

    if (request->response == CLKS_NULL) {
        return CLKS_NULL;
    }

    if (request->response->framebuffer_count < 1) {
        return CLKS_NULL;
    }

    return request->response->framebuffers[0];
}

const struct limine_memmap_response *clks_boot_get_memmap(void) {
    volatile struct limine_memmap_request *request = &limine_memmap_request;

    if (request->response == CLKS_NULL) {
        return CLKS_NULL;
    }

    return request->response;
}

const struct limine_file *clks_boot_get_executable_file(void) {
    volatile struct limine_executable_file_request *request = &limine_executable_file_request;

    if (request->response == CLKS_NULL) {
        return CLKS_NULL;
    }

    return request->response->executable_file;
}

const char *clks_boot_get_cmdline(void) {
    const struct limine_file *file = clks_boot_get_executable_file();

    if (file == CLKS_NULL || file->string == CLKS_NULL) {
        return "";
    }

    return file->string;
}

static clks_bool clks_boot_token_matches(const char *token, const char *name, usize name_len) {
    usize i;

    if (token == CLKS_NULL || name == CLKS_NULL || name_len == 0U) {
        return CLKS_FALSE;
    }

    for (i = 0U; i < name_len; i++) {
        if (token[i] != name[i]) {
            return CLKS_FALSE;
        }
    }

    if (token[name_len] == '\0' || token[name_len] == ' ' || token[name_len] == '\t') {
        return CLKS_TRUE;
    }

    if (token[name_len] == '=') {
        char value = token[name_len + 1U];
        return (value == '1' || value == 'y' || value == 'Y' || value == 't' || value == 'T') ? CLKS_TRUE
                                                                                              : CLKS_FALSE;
    }

    return CLKS_FALSE;
}

clks_bool clks_boot_cmdline_flag_enabled(const char *name) {
    const char *cmdline = clks_boot_get_cmdline();
    usize name_len = 0U;
    usize i = 0U;

    if (name == CLKS_NULL || name[0] == '\0') {
        return CLKS_FALSE;
    }

    while (name[name_len] != '\0') {
        name_len++;
    }

    while (cmdline[i] != '\0') {
        while (cmdline[i] == ' ' || cmdline[i] == '\t') {
            i++;
        }
        if (cmdline[i] == '\0') {
            break;
        }

        if (clks_boot_token_matches(cmdline + i, name, name_len) == CLKS_TRUE) {
            return CLKS_TRUE;
        }

        while (cmdline[i] != '\0' && cmdline[i] != ' ' && cmdline[i] != '\t') {
            i++;
        }
    }

    return CLKS_FALSE;
}

clks_bool clks_boot_rescue_mode(void) {
    return (clks_boot_cmdline_flag_enabled("clks.rescue") == CLKS_TRUE ||
            clks_boot_cmdline_flag_enabled("rescue") == CLKS_TRUE)
               ? CLKS_TRUE
               : CLKS_FALSE;
}

u64 clks_boot_get_module_count(void) {
    volatile struct limine_module_request *request = &limine_module_request;

    if (request->response == CLKS_NULL) {
        return 0ULL;
    }

    return request->response->module_count;
}

const struct limine_file *clks_boot_get_module(u64 index) {
    volatile struct limine_module_request *request = &limine_module_request;

    if (request->response == CLKS_NULL) {
        return CLKS_NULL;
    }

    if (index >= request->response->module_count) {
        return CLKS_NULL;
    }

    return request->response->modules[index];
}

u64 clks_boot_get_hhdm_offset(void) {
    volatile struct limine_hhdm_request *request = &limine_hhdm_request;

    if (request->response == CLKS_NULL) {
        return 0ULL;
    }

    return request->response->offset;
}

void *clks_boot_phys_to_virt(u64 phys_addr) {
    u64 offset = clks_boot_get_hhdm_offset();

    if (offset == 0ULL) {
        return CLKS_NULL;
    }

    return (void *)(usize)(phys_addr + offset);
}
