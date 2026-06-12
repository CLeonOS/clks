static i32 clks_exec_dynlib_alloc_slot(void) {
    u32 i;

    for (i = 0U; i < CLKS_EXEC_DYNLIB_MAX; i++) {
        if (clks_exec_dynlib_table[i].used == CLKS_FALSE) {
            return (i32)i;
        }
    }

    return -1;
}

static i32 clks_exec_dynlib_find_by_handle(u64 handle) {
    u32 i;

    for (i = 0U; i < CLKS_EXEC_DYNLIB_MAX; i++) {
        if (clks_exec_dynlib_table[i].used == CLKS_TRUE && clks_exec_dynlib_table[i].handle == handle) {
            return (i32)i;
        }
    }

    return -1;
}

static i32 clks_exec_dynlib_find_by_owner_path(u64 owner_pid, const char *path) {
    u32 i;

    if (path == CLKS_NULL) {
        return -1;
    }

    for (i = 0U; i < CLKS_EXEC_DYNLIB_MAX; i++) {
        if (clks_exec_dynlib_table[i].used != CLKS_TRUE) {
            continue;
        }

        if (clks_exec_dynlib_table[i].owner_pid == owner_pid &&
            clks_strcmp(clks_exec_dynlib_table[i].path, path) == 0) {
            return (i32)i;
        }
    }

    return -1;
}

static u64 clks_exec_dynlib_alloc_handle(void) {
    u64 handle = clks_exec_next_dynlib_handle;

    clks_exec_next_dynlib_handle++;
    if (clks_exec_next_dynlib_handle == 0ULL) {
        clks_exec_next_dynlib_handle = 1ULL;
    }

    if (handle == 0ULL) {
        handle = clks_exec_next_dynlib_handle;
        clks_exec_next_dynlib_handle++;
        if (clks_exec_next_dynlib_handle == 0ULL) {
            clks_exec_next_dynlib_handle = 1ULL;
        }
    }

    return handle;
}

static void clks_exec_dynlib_slot_reset(struct clks_exec_dynlib_slot *slot) {
    if (slot == CLKS_NULL) {
        return;
    }

    if (slot->used == CLKS_TRUE && slot->loaded.image_base != CLKS_NULL) {
        clks_elf64_unload(&slot->loaded);
    }

    clks_memset(slot, 0, sizeof(*slot));
}

static void clks_exec_dynlib_release_owner(u64 owner_pid) {
    u32 i;

    if (owner_pid == 0ULL) {
        return;
    }

    for (i = 0U; i < CLKS_EXEC_DYNLIB_MAX; i++) {
        if (clks_exec_dynlib_table[i].used == CLKS_TRUE && clks_exec_dynlib_table[i].owner_pid == owner_pid) {
            clks_exec_dynlib_slot_reset(&clks_exec_dynlib_table[i]);
        }
    }
}

static clks_bool clks_exec_dynlib_elf_header_ok(const struct clks_exec_elf64_ehdr *eh) {
    if (eh == CLKS_NULL) {
        return CLKS_FALSE;
    }

    if (eh->e_ident[0] != CLKS_EXEC_ELF64_MAGIC_0 || eh->e_ident[1] != CLKS_EXEC_ELF64_MAGIC_1 ||
        eh->e_ident[2] != CLKS_EXEC_ELF64_MAGIC_2 || eh->e_ident[3] != CLKS_EXEC_ELF64_MAGIC_3) {
        return CLKS_FALSE;
    }

    if (eh->e_ident[4] != CLKS_EXEC_ELF64_CLASS_64 || eh->e_ident[5] != CLKS_EXEC_ELF64_DATA_LSB) {
        return CLKS_FALSE;
    }

    if (eh->e_ident[6] != CLKS_EXEC_ELF64_VERSION || eh->e_version != CLKS_EXEC_ELF64_VERSION) {
        return CLKS_FALSE;
    }

    if (eh->e_shentsize != sizeof(struct clks_exec_elf64_shdr)) {
        return CLKS_FALSE;
    }

    return CLKS_TRUE;
}

static clks_bool clks_exec_dynlib_symbol_name_ptr(const char *strtab, u64 strtab_size, u32 st_name,
                                                  const char **out_name) {
    u64 i;

    if (out_name != CLKS_NULL) {
        *out_name = CLKS_NULL;
    }

    if (strtab == CLKS_NULL || out_name == CLKS_NULL || st_name >= strtab_size) {
        return CLKS_FALSE;
    }

    for (i = (u64)st_name; i < strtab_size; i++) {
        if (strtab[i] == '\0') {
            *out_name = &strtab[st_name];
            return CLKS_TRUE;
        }
    }

    return CLKS_FALSE;
}

static clks_bool clks_exec_dynlib_resolve_symbol(const struct clks_exec_dynlib_slot *slot, const char *symbol,
                                                 u64 *out_addr) {
    const struct clks_exec_elf64_ehdr *eh;
    const struct clks_exec_elf64_shdr *shdrs;
    const u8 *image;
    u64 i;
    u64 sh_table_size;

    if (out_addr != CLKS_NULL) {
        *out_addr = 0ULL;
    }

    if (slot == CLKS_NULL || slot->used != CLKS_TRUE || symbol == CLKS_NULL || symbol[0] == '\0' ||
        out_addr == CLKS_NULL) {
        return CLKS_FALSE;
    }

    image = (const u8 *)slot->file_image;

    if (image == CLKS_NULL || slot->file_size < sizeof(struct clks_exec_elf64_ehdr)) {
        return CLKS_FALSE;
    }

    eh = (const struct clks_exec_elf64_ehdr *)image;

    if (clks_exec_dynlib_elf_header_ok(eh) == CLKS_FALSE || eh->e_shnum == 0U || eh->e_shoff == 0ULL) {
        return CLKS_FALSE;
    }

    sh_table_size = (u64)eh->e_shnum * (u64)eh->e_shentsize;
    if (clks_exec_range_ok(eh->e_shoff, sh_table_size, slot->file_size) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    shdrs = (const struct clks_exec_elf64_shdr *)(image + (usize)eh->e_shoff);

    for (i = 0ULL; i < (u64)eh->e_shnum; i++) {
        const struct clks_exec_elf64_shdr *symtab = &shdrs[i];
        const struct clks_exec_elf64_shdr *strtab;
        const struct clks_exec_elf64_sym *symbols;
        const char *strings;
        u64 sym_count;
        u64 j;

        if (symtab->sh_type != CLKS_EXEC_ELF64_SHT_SYMTAB && symtab->sh_type != CLKS_EXEC_ELF64_SHT_DYNSYM) {
            continue;
        }

        if (symtab->sh_entsize != sizeof(struct clks_exec_elf64_sym) || symtab->sh_link >= eh->e_shnum ||
            symtab->sh_size < symtab->sh_entsize) {
            continue;
        }

        if (clks_exec_range_ok(symtab->sh_offset, symtab->sh_size, slot->file_size) == CLKS_FALSE) {
            continue;
        }

        strtab = &shdrs[symtab->sh_link];
        if (clks_exec_range_ok(strtab->sh_offset, strtab->sh_size, slot->file_size) == CLKS_FALSE) {
            continue;
        }

        symbols = (const struct clks_exec_elf64_sym *)(image + (usize)symtab->sh_offset);
        strings = (const char *)(image + (usize)strtab->sh_offset);
        sym_count = symtab->sh_size / symtab->sh_entsize;

        for (j = 0ULL; j < sym_count; j++) {
            const struct clks_exec_elf64_sym *sym = &symbols[j];
            const char *name_ptr;
            u8 symbol_type;
            u64 offset;

            if (sym->st_name == 0U || sym->st_shndx == CLKS_EXEC_ELF64_SHN_UNDEF || sym->st_value == 0ULL) {
                continue;
            }

            symbol_type = (u8)(sym->st_info & 0x0FU);
            if (symbol_type != CLKS_EXEC_ELF64_STT_FUNC && symbol_type != CLKS_EXEC_ELF64_STT_NOTYPE) {
                continue;
            }

            if (clks_exec_dynlib_symbol_name_ptr(strings, strtab->sh_size, sym->st_name, &name_ptr) == CLKS_FALSE) {
                continue;
            }

            if (clks_strcmp(name_ptr, symbol) != 0) {
                continue;
            }

            if (sym->st_value < slot->loaded.image_vaddr_base) {
                continue;
            }

            offset = sym->st_value - slot->loaded.image_vaddr_base;
            if (offset >= slot->loaded.image_size) {
                continue;
            }

            *out_addr = (u64)((u8 *)slot->loaded.image_base + (usize)offset);
            return CLKS_TRUE;
        }
    }

    return CLKS_FALSE;
}

