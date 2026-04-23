cmake_minimum_required(VERSION 3.20)
project(CLKS NONE)

set(NO_COLOR 0 CACHE STRING "Disable colored log output when set to 1")
include("${CMAKE_SOURCE_DIR}/cmake/log.cmake")
set(CL_LOG_EMIT_SCRIPT "${CMAKE_SOURCE_DIR}/cmake/log_emit.cmake")

set(CC "x86_64-elf-gcc" CACHE STRING "Kernel C compiler")
set(KERNEL_CXX "g++" CACHE STRING "Kernel C++ compiler")
set(LD "x86_64-elf-ld" CACHE STRING "Kernel linker")
set(RUSTC "rustc" CACHE STRING "Rust compiler")
set(NM "nm" CACHE STRING "nm executable")
set(ADDR2LINE "addr2line" CACHE STRING "addr2line executable")
set(OBJCOPY_FOR_TARGET "llvm-objcopy" CACHE STRING "objcopy tool for checks")
set(OBJDUMP_FOR_TARGET "llvm-objdump" CACHE STRING "objdump tool for checks")
set(READELF_FOR_TARGET "llvm-readelf" CACHE STRING "readelf tool for checks")
set(MAKE_TOOL "make" CACHE STRING "make executable")
set(SH_TOOL "sh" CACHE STRING "POSIX shell executable")

function(resolve_tool_with_fallback VAR_NAME)
    set(_fallbacks ${ARGN})
    set(_requested "${${VAR_NAME}}")

    if("${_requested}" STREQUAL "")
        cl_log_error("empty tool variable: ${VAR_NAME}")
    endif()

    if(IS_ABSOLUTE "${_requested}")
        if(NOT EXISTS "${_requested}")
            cl_log_error("${VAR_NAME} not found: ${_requested}")
        endif()
        set(_resolved "${_requested}")
    else()
        unset(_requested_path CACHE)
        unset(_requested_path)
        find_program(_requested_path NAMES "${_requested}")
        if(_requested_path)
            set(_resolved "${_requested_path}")
        else()
            set(_resolved "")
            foreach(_cand IN LISTS _fallbacks)
                unset(_cand_path CACHE)
                unset(_cand_path)
                find_program(_cand_path NAMES "${_cand}")
                if(_cand_path)
                    cl_log_warn("${VAR_NAME} '${_requested}' not found; fallback to '${_cand}'")
                    set(_resolved "${_cand_path}")
                    break()
                endif()
            endforeach()
            if("${_resolved}" STREQUAL "")
                cl_log_error("${VAR_NAME} tool not found: '${_requested}', fallbacks='${_fallbacks}'")
            endif()
        endif()
    endif()

    get_property(_help CACHE "${VAR_NAME}" PROPERTY HELPSTRING)
    if("${_help}" STREQUAL "")
        set(_help "tool path/name")
    endif()
    set(${VAR_NAME} "${_resolved}" CACHE STRING "${_help}" FORCE)
    set(${VAR_NAME} "${_resolved}" PARENT_SCOPE)
endfunction()

set(CLKS_ARCH "x86_64" CACHE STRING "Target CLKS arch")
if(NOT CLKS_ARCH STREQUAL "x86_64")
    cl_log_error("CLKS_ARCH=${CLKS_ARCH} is not supported by this CMake build yet; use x86_64")
endif()

set(CLKS_BUILD_ROOT "${CMAKE_SOURCE_DIR}/build/x86_64" CACHE PATH "CLKS build output root")
set(OBJ_ROOT "${CLKS_BUILD_ROOT}/obj")
set(KERNEL_ELF "${CLKS_BUILD_ROOT}/clks_kernel.elf")
set(KERNEL_RUST_LIB "${CLKS_BUILD_ROOT}/libclks_kernel_rust.a")
set(KERNEL_SYMBOLS_FILE "${CLKS_BUILD_ROOT}/kernel.sym")
set(LINKER_SCRIPT "${CMAKE_SOURCE_DIR}/arch/${CLKS_ARCH}/linker.ld")
set(CLKS_MENUCONFIG_CMAKE "${CMAKE_SOURCE_DIR}/configs/menuconfig/config.cmake" CACHE FILEPATH "menuconfig generated CLKS CMake config")

if(EXISTS "${CLKS_MENUCONFIG_CMAKE}")
    include("${CLKS_MENUCONFIG_CMAKE}")
    cl_log_info("menuconfig loaded from ${CLKS_MENUCONFIG_CMAKE}")
elseif(EXISTS "${CMAKE_SOURCE_DIR}/configs/menuconfig/config.clks.cmake")
    include("${CMAKE_SOURCE_DIR}/configs/menuconfig/config.clks.cmake")
    cl_log_info("menuconfig loaded from ${CMAKE_SOURCE_DIR}/configs/menuconfig/config.clks.cmake")
endif()

function(cl_set_bool_cache VAR_NAME DEFAULT_VALUE DOC_TEXT)
    set(_raw_value "")
    set(_has_value OFF)

    if(DEFINED ${VAR_NAME})
        set(_raw_value "${${VAR_NAME}}")
        set(_has_value ON)
    elseif(DEFINED ${VAR_NAME}_IS_ENABLED)
        set(_raw_value "${${VAR_NAME}_IS_ENABLED}")
        set(_has_value ON)
    endif()

    if(NOT _has_value)
        set(${VAR_NAME} ${DEFAULT_VALUE} CACHE BOOL "${DOC_TEXT}")
    else()
        string(TOUPPER "${_raw_value}" _raw_upper)
        if(_raw_upper STREQUAL "Y" OR _raw_upper STREQUAL "M" OR _raw_upper STREQUAL "ON" OR _raw_upper STREQUAL "TRUE" OR _raw_upper STREQUAL "1")
            set(_bool_value ON)
        else()
            set(_bool_value OFF)
        endif()
        set(${VAR_NAME} ${_bool_value} CACHE BOOL "${DOC_TEXT}" FORCE)
    endif()
endfunction()

function(cl_bool_to_int BOOL_VAR OUT_VAR)
    if(${BOOL_VAR})
        set(${OUT_VAR} 1 PARENT_SCOPE)
    else()
        set(${OUT_VAR} 0 PARENT_SCOPE)
    endif()
endfunction()

cl_set_bool_cache(CLEONOS_CLKS_ENABLE_AUDIO ON "Initialize kernel audio subsystem")
cl_set_bool_cache(CLEONOS_CLKS_ENABLE_MOUSE ON "Initialize kernel mouse subsystem")
cl_set_bool_cache(CLEONOS_CLKS_ENABLE_DESKTOP ON "Initialize desktop compositor subsystem")
cl_set_bool_cache(CLEONOS_CLKS_ENABLE_DRIVER_MANAGER ON "Initialize kernel driver manager")
cl_set_bool_cache(CLEONOS_CLKS_ENABLE_KELF ON "Enable KELF app dispatcher")
cl_set_bool_cache(CLEONOS_CLKS_ENABLE_USERLAND_AUTO_EXEC ON "Auto exec /shell/shell.elf on boot")
cl_set_bool_cache(CLEONOS_CLKS_ENABLE_HEAP_SELFTEST ON "Run heap selftest at boot")
cl_set_bool_cache(CLEONOS_CLKS_ENABLE_EXTERNAL_PSF ON "Load /system/tty.psf external font")
cl_set_bool_cache(CLEONOS_CLKS_ENABLE_KEYBOARD ON "Initialize keyboard input subsystem")
cl_set_bool_cache(CLEONOS_CLKS_ENABLE_ELFRUNNER_PROBE ON "Run ELFRUNNER kernel ELF probe")
cl_set_bool_cache(CLEONOS_CLKS_ENABLE_KLOGD_TASK ON "Enable scheduler klogd task")
cl_set_bool_cache(CLEONOS_CLKS_ENABLE_KWORKER_TASK ON "Enable scheduler kworker task")
cl_set_bool_cache(CLEONOS_CLKS_ENABLE_USRD_TASK ON "Enable scheduler usrd task")
cl_set_bool_cache(CLEONOS_CLKS_ENABLE_BOOT_VIDEO_LOG ON "Print boot framebuffer geometry logs")
cl_set_bool_cache(CLEONOS_CLKS_ENABLE_PMM_STATS_LOG ON "Print PMM stats logs")
cl_set_bool_cache(CLEONOS_CLKS_ENABLE_HEAP_STATS_LOG ON "Print heap stats logs")
cl_set_bool_cache(CLEONOS_CLKS_ENABLE_FS_ROOT_LOG ON "Print root children FS log")
cl_set_bool_cache(CLEONOS_CLKS_ENABLE_SYSTEM_DIR_CHECK ON "Require /system directory sanity check")
cl_set_bool_cache(CLEONOS_CLKS_ENABLE_ELFRUNNER_INIT ON "Initialize ELFRUNNER framework")
cl_set_bool_cache(CLEONOS_CLKS_ENABLE_SYSCALL_TICK_QUERY ON "Query syscall timer ticks at boot")
cl_set_bool_cache(CLEONOS_CLKS_ENABLE_TTY_READY_LOG ON "Print TTY ready logs")
cl_set_bool_cache(CLEONOS_CLKS_ENABLE_IDLE_DEBUG_LOG ON "Print idle loop debug log")
cl_set_bool_cache(CLEONOS_CLKS_ENABLE_PROCFS ON "Enable virtual /proc procfs syscall layer")
cl_set_bool_cache(CLEONOS_CLKS_ENABLE_EXEC_SERIAL_LOG ON "Enable EXEC info logs on serial output")
cl_set_bool_cache(CLEONOS_CLKS_ENABLE_USC ON "Enable UserSafeController dangerous syscall confirmations")
cl_set_bool_cache(CLEONOS_CLKS_ENABLE_USC_SC_FS_MKDIR ON "USC intercept policy: FS_MKDIR")
cl_set_bool_cache(CLEONOS_CLKS_ENABLE_USC_SC_FS_WRITE ON "USC intercept policy: FS_WRITE")
cl_set_bool_cache(CLEONOS_CLKS_ENABLE_USC_SC_FS_APPEND ON "USC intercept policy: FS_APPEND")
cl_set_bool_cache(CLEONOS_CLKS_ENABLE_USC_SC_FS_REMOVE ON "USC intercept policy: FS_REMOVE")
cl_set_bool_cache(CLEONOS_CLKS_ENABLE_USC_SC_EXEC_PATH ON "USC intercept policy: EXEC_PATH")
cl_set_bool_cache(CLEONOS_CLKS_ENABLE_USC_SC_EXEC_PATHV ON "USC intercept policy: EXEC_PATHV")
cl_set_bool_cache(CLEONOS_CLKS_ENABLE_USC_SC_EXEC_PATHV_IO ON "USC intercept policy: EXEC_PATHV_IO")
cl_set_bool_cache(CLEONOS_CLKS_ENABLE_USC_SC_SPAWN_PATH ON "USC intercept policy: SPAWN_PATH")
cl_set_bool_cache(CLEONOS_CLKS_ENABLE_USC_SC_SPAWN_PATHV ON "USC intercept policy: SPAWN_PATHV")
cl_set_bool_cache(CLEONOS_CLKS_ENABLE_USC_SC_PROC_KILL ON "USC intercept policy: PROC_KILL")
cl_set_bool_cache(CLEONOS_CLKS_ENABLE_USC_SC_SHUTDOWN ON "USC intercept policy: SHUTDOWN")
cl_set_bool_cache(CLEONOS_CLKS_ENABLE_USC_SC_RESTART ON "USC intercept policy: RESTART")
cl_set_bool_cache(CLEONOS_CLKS_ENABLE_KBD_TTY_SWITCH_HOTKEY ON "Enable ALT+F1..F4 TTY switch hotkey")
cl_set_bool_cache(CLEONOS_CLKS_ENABLE_KBD_CTRL_SHORTCUTS ON "Enable Ctrl+A/C/V keyboard shortcuts")
cl_set_bool_cache(CLEONOS_CLKS_ENABLE_KBD_FORCE_STOP_HOTKEY ON "Enable Ctrl+Alt+C force-stop hotkey")
cl_set_bool_cache(CLEONOS_CLKS_ENABLE_USER_INIT_SCRIPT_PROBE ON "Probe /shell/init.cmd during userland init")
cl_set_bool_cache(CLEONOS_CLKS_ENABLE_USER_SYSTEM_APP_PROBE ON "Probe /system apps (elfrunner/memc) during userland init")
cl_set_bool_cache(CLEONOS_CLKS_ENABLE_SCHED_TASK_COUNT_LOG ON "Print scheduler task count during boot")
cl_set_bool_cache(CLEONOS_CLKS_ENABLE_INTERRUPT_READY_LOG ON "Print interrupt ready log after IDT/PIC init")
cl_set_bool_cache(CLEONOS_CLKS_ENABLE_SHELL_MODE_LOG ON "Print default shell mode log during boot")

cl_bool_to_int(CLEONOS_CLKS_ENABLE_AUDIO CLKS_CFG_AUDIO_INT)
cl_bool_to_int(CLEONOS_CLKS_ENABLE_MOUSE CLKS_CFG_MOUSE_INT)
cl_bool_to_int(CLEONOS_CLKS_ENABLE_DESKTOP CLKS_CFG_DESKTOP_INT)
cl_bool_to_int(CLEONOS_CLKS_ENABLE_DRIVER_MANAGER CLKS_CFG_DRIVER_MANAGER_INT)
cl_bool_to_int(CLEONOS_CLKS_ENABLE_KELF CLKS_CFG_KELF_INT)
cl_bool_to_int(CLEONOS_CLKS_ENABLE_USERLAND_AUTO_EXEC CLKS_CFG_USERLAND_AUTO_EXEC_INT)
cl_bool_to_int(CLEONOS_CLKS_ENABLE_HEAP_SELFTEST CLKS_CFG_HEAP_SELFTEST_INT)
cl_bool_to_int(CLEONOS_CLKS_ENABLE_EXTERNAL_PSF CLKS_CFG_EXTERNAL_PSF_INT)
cl_bool_to_int(CLEONOS_CLKS_ENABLE_KEYBOARD CLKS_CFG_KEYBOARD_INT)
cl_bool_to_int(CLEONOS_CLKS_ENABLE_ELFRUNNER_PROBE CLKS_CFG_ELFRUNNER_PROBE_INT)
cl_bool_to_int(CLEONOS_CLKS_ENABLE_KLOGD_TASK CLKS_CFG_KLOGD_TASK_INT)
cl_bool_to_int(CLEONOS_CLKS_ENABLE_KWORKER_TASK CLKS_CFG_KWORKER_TASK_INT)
cl_bool_to_int(CLEONOS_CLKS_ENABLE_USRD_TASK CLKS_CFG_USRD_TASK_INT)
cl_bool_to_int(CLEONOS_CLKS_ENABLE_BOOT_VIDEO_LOG CLKS_CFG_BOOT_VIDEO_LOG_INT)
cl_bool_to_int(CLEONOS_CLKS_ENABLE_PMM_STATS_LOG CLKS_CFG_PMM_STATS_LOG_INT)
cl_bool_to_int(CLEONOS_CLKS_ENABLE_HEAP_STATS_LOG CLKS_CFG_HEAP_STATS_LOG_INT)
cl_bool_to_int(CLEONOS_CLKS_ENABLE_FS_ROOT_LOG CLKS_CFG_FS_ROOT_LOG_INT)
cl_bool_to_int(CLEONOS_CLKS_ENABLE_SYSTEM_DIR_CHECK CLKS_CFG_SYSTEM_DIR_CHECK_INT)
cl_bool_to_int(CLEONOS_CLKS_ENABLE_ELFRUNNER_INIT CLKS_CFG_ELFRUNNER_INIT_INT)
cl_bool_to_int(CLEONOS_CLKS_ENABLE_SYSCALL_TICK_QUERY CLKS_CFG_SYSCALL_TICK_QUERY_INT)
cl_bool_to_int(CLEONOS_CLKS_ENABLE_TTY_READY_LOG CLKS_CFG_TTY_READY_LOG_INT)
cl_bool_to_int(CLEONOS_CLKS_ENABLE_IDLE_DEBUG_LOG CLKS_CFG_IDLE_DEBUG_LOG_INT)
cl_bool_to_int(CLEONOS_CLKS_ENABLE_PROCFS CLKS_CFG_PROCFS_INT)
cl_bool_to_int(CLEONOS_CLKS_ENABLE_EXEC_SERIAL_LOG CLKS_CFG_EXEC_SERIAL_LOG_INT)
cl_bool_to_int(CLEONOS_CLKS_ENABLE_USC CLKS_CFG_USC_INT)
cl_bool_to_int(CLEONOS_CLKS_ENABLE_USC_SC_FS_MKDIR CLKS_CFG_USC_SC_FS_MKDIR_INT)
cl_bool_to_int(CLEONOS_CLKS_ENABLE_USC_SC_FS_WRITE CLKS_CFG_USC_SC_FS_WRITE_INT)
cl_bool_to_int(CLEONOS_CLKS_ENABLE_USC_SC_FS_APPEND CLKS_CFG_USC_SC_FS_APPEND_INT)
cl_bool_to_int(CLEONOS_CLKS_ENABLE_USC_SC_FS_REMOVE CLKS_CFG_USC_SC_FS_REMOVE_INT)
cl_bool_to_int(CLEONOS_CLKS_ENABLE_USC_SC_EXEC_PATH CLKS_CFG_USC_SC_EXEC_PATH_INT)
cl_bool_to_int(CLEONOS_CLKS_ENABLE_USC_SC_EXEC_PATHV CLKS_CFG_USC_SC_EXEC_PATHV_INT)
cl_bool_to_int(CLEONOS_CLKS_ENABLE_USC_SC_EXEC_PATHV_IO CLKS_CFG_USC_SC_EXEC_PATHV_IO_INT)
cl_bool_to_int(CLEONOS_CLKS_ENABLE_USC_SC_SPAWN_PATH CLKS_CFG_USC_SC_SPAWN_PATH_INT)
cl_bool_to_int(CLEONOS_CLKS_ENABLE_USC_SC_SPAWN_PATHV CLKS_CFG_USC_SC_SPAWN_PATHV_INT)
cl_bool_to_int(CLEONOS_CLKS_ENABLE_USC_SC_PROC_KILL CLKS_CFG_USC_SC_PROC_KILL_INT)
cl_bool_to_int(CLEONOS_CLKS_ENABLE_USC_SC_SHUTDOWN CLKS_CFG_USC_SC_SHUTDOWN_INT)
cl_bool_to_int(CLEONOS_CLKS_ENABLE_USC_SC_RESTART CLKS_CFG_USC_SC_RESTART_INT)
cl_bool_to_int(CLEONOS_CLKS_ENABLE_KBD_TTY_SWITCH_HOTKEY CLKS_CFG_KBD_TTY_SWITCH_HOTKEY_INT)
cl_bool_to_int(CLEONOS_CLKS_ENABLE_KBD_CTRL_SHORTCUTS CLKS_CFG_KBD_CTRL_SHORTCUTS_INT)
cl_bool_to_int(CLEONOS_CLKS_ENABLE_KBD_FORCE_STOP_HOTKEY CLKS_CFG_KBD_FORCE_STOP_HOTKEY_INT)
cl_bool_to_int(CLEONOS_CLKS_ENABLE_USER_INIT_SCRIPT_PROBE CLKS_CFG_USER_INIT_SCRIPT_PROBE_INT)
cl_bool_to_int(CLEONOS_CLKS_ENABLE_USER_SYSTEM_APP_PROBE CLKS_CFG_USER_SYSTEM_APP_PROBE_INT)
cl_bool_to_int(CLEONOS_CLKS_ENABLE_SCHED_TASK_COUNT_LOG CLKS_CFG_SCHED_TASK_COUNT_LOG_INT)
cl_bool_to_int(CLEONOS_CLKS_ENABLE_INTERRUPT_READY_LOG CLKS_CFG_INTERRUPT_READY_LOG_INT)
cl_bool_to_int(CLEONOS_CLKS_ENABLE_SHELL_MODE_LOG CLKS_CFG_SHELL_MODE_LOG_INT)

set(CFLAGS_COMMON
    -std=c11
    -ffreestanding
    -fno-stack-protector
    -fno-builtin
    -g
    -Wall
    -Wextra
    -Werror
    "-I${CMAKE_SOURCE_DIR}/include"
)

set(CXXFLAGS_COMMON
    -std=c++17
    -ffreestanding
    -fno-stack-protector
    -fno-builtin
    -fno-exceptions
    -fno-rtti
    -fno-threadsafe-statics
    -fno-use-cxa-atexit
    -g
    -Wall
    -Wextra
    -Werror
    "-I${CMAKE_SOURCE_DIR}/include"
)

set(ARCH_CFLAGS
    -DCLKS_ARCH_X86_64=1
    "-DCLKS_CFG_AUDIO=${CLKS_CFG_AUDIO_INT}"
    "-DCLKS_CFG_MOUSE=${CLKS_CFG_MOUSE_INT}"
    "-DCLKS_CFG_DESKTOP=${CLKS_CFG_DESKTOP_INT}"
    "-DCLKS_CFG_DRIVER_MANAGER=${CLKS_CFG_DRIVER_MANAGER_INT}"
    "-DCLKS_CFG_KELF=${CLKS_CFG_KELF_INT}"
    "-DCLKS_CFG_USERLAND_AUTO_EXEC=${CLKS_CFG_USERLAND_AUTO_EXEC_INT}"
    "-DCLKS_CFG_HEAP_SELFTEST=${CLKS_CFG_HEAP_SELFTEST_INT}"
    "-DCLKS_CFG_EXTERNAL_PSF=${CLKS_CFG_EXTERNAL_PSF_INT}"
    "-DCLKS_CFG_KEYBOARD=${CLKS_CFG_KEYBOARD_INT}"
    "-DCLKS_CFG_ELFRUNNER_PROBE=${CLKS_CFG_ELFRUNNER_PROBE_INT}"
    "-DCLKS_CFG_KLOGD_TASK=${CLKS_CFG_KLOGD_TASK_INT}"
    "-DCLKS_CFG_KWORKER_TASK=${CLKS_CFG_KWORKER_TASK_INT}"
    "-DCLKS_CFG_USRD_TASK=${CLKS_CFG_USRD_TASK_INT}"
    "-DCLKS_CFG_BOOT_VIDEO_LOG=${CLKS_CFG_BOOT_VIDEO_LOG_INT}"
    "-DCLKS_CFG_PMM_STATS_LOG=${CLKS_CFG_PMM_STATS_LOG_INT}"
    "-DCLKS_CFG_HEAP_STATS_LOG=${CLKS_CFG_HEAP_STATS_LOG_INT}"
    "-DCLKS_CFG_FS_ROOT_LOG=${CLKS_CFG_FS_ROOT_LOG_INT}"
    "-DCLKS_CFG_SYSTEM_DIR_CHECK=${CLKS_CFG_SYSTEM_DIR_CHECK_INT}"
    "-DCLKS_CFG_ELFRUNNER_INIT=${CLKS_CFG_ELFRUNNER_INIT_INT}"
    "-DCLKS_CFG_SYSCALL_TICK_QUERY=${CLKS_CFG_SYSCALL_TICK_QUERY_INT}"
    "-DCLKS_CFG_TTY_READY_LOG=${CLKS_CFG_TTY_READY_LOG_INT}"
    "-DCLKS_CFG_IDLE_DEBUG_LOG=${CLKS_CFG_IDLE_DEBUG_LOG_INT}"
    "-DCLKS_CFG_PROCFS=${CLKS_CFG_PROCFS_INT}"
    "-DCLKS_CFG_EXEC_SERIAL_LOG=${CLKS_CFG_EXEC_SERIAL_LOG_INT}"
    "-DCLKS_CFG_USC=${CLKS_CFG_USC_INT}"
    "-DCLKS_CFG_USC_SC_FS_MKDIR=${CLKS_CFG_USC_SC_FS_MKDIR_INT}"
    "-DCLKS_CFG_USC_SC_FS_WRITE=${CLKS_CFG_USC_SC_FS_WRITE_INT}"
    "-DCLKS_CFG_USC_SC_FS_APPEND=${CLKS_CFG_USC_SC_FS_APPEND_INT}"
    "-DCLKS_CFG_USC_SC_FS_REMOVE=${CLKS_CFG_USC_SC_FS_REMOVE_INT}"
    "-DCLKS_CFG_USC_SC_EXEC_PATH=${CLKS_CFG_USC_SC_EXEC_PATH_INT}"
    "-DCLKS_CFG_USC_SC_EXEC_PATHV=${CLKS_CFG_USC_SC_EXEC_PATHV_INT}"
    "-DCLKS_CFG_USC_SC_EXEC_PATHV_IO=${CLKS_CFG_USC_SC_EXEC_PATHV_IO_INT}"
    "-DCLKS_CFG_USC_SC_SPAWN_PATH=${CLKS_CFG_USC_SC_SPAWN_PATH_INT}"
    "-DCLKS_CFG_USC_SC_SPAWN_PATHV=${CLKS_CFG_USC_SC_SPAWN_PATHV_INT}"
    "-DCLKS_CFG_USC_SC_PROC_KILL=${CLKS_CFG_USC_SC_PROC_KILL_INT}"
    "-DCLKS_CFG_USC_SC_SHUTDOWN=${CLKS_CFG_USC_SC_SHUTDOWN_INT}"
    "-DCLKS_CFG_USC_SC_RESTART=${CLKS_CFG_USC_SC_RESTART_INT}"
    "-DCLKS_CFG_KBD_TTY_SWITCH_HOTKEY=${CLKS_CFG_KBD_TTY_SWITCH_HOTKEY_INT}"
    "-DCLKS_CFG_KBD_CTRL_SHORTCUTS=${CLKS_CFG_KBD_CTRL_SHORTCUTS_INT}"
    "-DCLKS_CFG_KBD_FORCE_STOP_HOTKEY=${CLKS_CFG_KBD_FORCE_STOP_HOTKEY_INT}"
    "-DCLKS_CFG_USER_INIT_SCRIPT_PROBE=${CLKS_CFG_USER_INIT_SCRIPT_PROBE_INT}"
    "-DCLKS_CFG_USER_SYSTEM_APP_PROBE=${CLKS_CFG_USER_SYSTEM_APP_PROBE_INT}"
    "-DCLKS_CFG_SCHED_TASK_COUNT_LOG=${CLKS_CFG_SCHED_TASK_COUNT_LOG_INT}"
    "-DCLKS_CFG_INTERRUPT_READY_LOG=${CLKS_CFG_INTERRUPT_READY_LOG_INT}"
    "-DCLKS_CFG_SHELL_MODE_LOG=${CLKS_CFG_SHELL_MODE_LOG_INT}"
    -m64
    -mno-red-zone
    -mcmodel=kernel
    -fno-pic
    -fno-pie
)

set(ASFLAGS_COMMON
    -ffreestanding
    "-I${CMAKE_SOURCE_DIR}/include"
)

set(LDFLAGS_COMMON
    -nostdlib
    -z
    max-page-size=0x1000
)

resolve_tool_with_fallback(CC gcc cc clang)
resolve_tool_with_fallback(KERNEL_CXX g++ clang++ x86_64-elf-g++)
resolve_tool_with_fallback(LD ld.lld ld)
resolve_tool_with_fallback(NM llvm-nm x86_64-elf-nm nm)
resolve_tool_with_fallback(ADDR2LINE llvm-addr2line x86_64-elf-addr2line addr2line)
resolve_tool_with_fallback(OBJCOPY_FOR_TARGET llvm-objcopy x86_64-linux-gnu-objcopy objcopy)
resolve_tool_with_fallback(OBJDUMP_FOR_TARGET llvm-objdump x86_64-linux-gnu-objdump objdump)
resolve_tool_with_fallback(READELF_FOR_TARGET llvm-readelf x86_64-linux-gnu-readelf readelf)

set(KERNEL_SOURCE_DIRS
    "${CMAKE_SOURCE_DIR}/kernel"
    "${CMAKE_SOURCE_DIR}/arch/${CLKS_ARCH}"
    "${CMAKE_SOURCE_DIR}/third_party"
)

set(C_SOURCES)
set(CPP_SOURCES)
set(ASM_SOURCES)
foreach(_src_dir IN LISTS KERNEL_SOURCE_DIRS)
    if(EXISTS "${_src_dir}")
        file(GLOB_RECURSE _c_abs CONFIGURE_DEPENDS "${_src_dir}/*.c")
        file(GLOB_RECURSE _cpp_abs CONFIGURE_DEPENDS "${_src_dir}/*.cpp")
        file(GLOB_RECURSE _s_abs CONFIGURE_DEPENDS "${_src_dir}/*.S")
        foreach(_abs IN LISTS _c_abs)
            file(RELATIVE_PATH _rel "${CMAKE_SOURCE_DIR}" "${_abs}")
            list(APPEND C_SOURCES "${_rel}")
        endforeach()
        foreach(_abs IN LISTS _cpp_abs)
            file(RELATIVE_PATH _rel "${CMAKE_SOURCE_DIR}" "${_abs}")
            list(APPEND CPP_SOURCES "${_rel}")
        endforeach()
        foreach(_abs IN LISTS _s_abs)
            file(RELATIVE_PATH _rel "${CMAKE_SOURCE_DIR}" "${_abs}")
            list(APPEND ASM_SOURCES "${_rel}")
        endforeach()
    endif()
endforeach()
list(REMOVE_DUPLICATES C_SOURCES)
list(REMOVE_DUPLICATES CPP_SOURCES)
list(REMOVE_DUPLICATES ASM_SOURCES)
list(SORT C_SOURCES)
list(SORT CPP_SOURCES)
list(SORT ASM_SOURCES)

set(CLKS_BOOT_LIMINE_SOURCE "kernel/boot/limine/limine_requests.c")
if(EXISTS "${CMAKE_SOURCE_DIR}/${CLKS_BOOT_LIMINE_SOURCE}")
    list(APPEND C_SOURCES "${CLKS_BOOT_LIMINE_SOURCE}")
    list(REMOVE_DUPLICATES C_SOURCES)
    list(SORT C_SOURCES)
else()
    cl_log_error("missing required boot source: ${CLKS_BOOT_LIMINE_SOURCE}")
endif()

file(GLOB_RECURSE KERNEL_INC_SOURCES_ABS CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/**/*.inc"
)
list(SORT KERNEL_INC_SOURCES_ABS)

if(NOT C_SOURCES)
    cl_log_error("no kernel C sources found")
endif()
if(NOT EXISTS "${LINKER_SCRIPT}")
    cl_log_error("missing linker script: ${LINKER_SCRIPT}")
endif()

function(add_kernel_c_object SRC OUTPUT_LIST_VAR)
    string(REGEX REPLACE "\\.c$" ".o" OBJ_REL "${SRC}")
    set(OBJ_PATH "${OBJ_ROOT}/${OBJ_REL}")
    get_filename_component(OBJ_DIR "${OBJ_PATH}" DIRECTORY)
    set(_deps "${CMAKE_SOURCE_DIR}/${SRC}" ${KERNEL_INC_SOURCES_ABS})
    set(_cflags ${CFLAGS_COMMON})

    if("${SRC}" MATCHES "^third_party/")
        list(APPEND _cflags
            -Wno-error
            -Wno-unused-function
            -Wno-unused-parameter
            -Wno-sign-compare
            -Wno-type-limits
            -Wno-missing-field-initializers
        )
    endif()

    add_custom_command(
        OUTPUT "${OBJ_PATH}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${OBJ_DIR}"
        COMMAND ${CC} ${_cflags} ${ARCH_CFLAGS} -c "${CMAKE_SOURCE_DIR}/${SRC}" -o "${OBJ_PATH}"
        DEPENDS ${_deps}
        VERBATIM
    )

    set(${OUTPUT_LIST_VAR} ${${OUTPUT_LIST_VAR}} "${OBJ_PATH}" PARENT_SCOPE)
endfunction()

function(add_kernel_asm_object SRC OUTPUT_LIST_VAR)
    string(REGEX REPLACE "\\.S$" ".o" OBJ_REL "${SRC}")
    set(OBJ_PATH "${OBJ_ROOT}/${OBJ_REL}")
    get_filename_component(OBJ_DIR "${OBJ_PATH}" DIRECTORY)
    set(_deps "${CMAKE_SOURCE_DIR}/${SRC}" ${KERNEL_INC_SOURCES_ABS})

    add_custom_command(
        OUTPUT "${OBJ_PATH}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${OBJ_DIR}"
        COMMAND ${CC} ${ASFLAGS_COMMON} ${ARCH_CFLAGS} -c "${CMAKE_SOURCE_DIR}/${SRC}" -o "${OBJ_PATH}"
        DEPENDS ${_deps}
        VERBATIM
    )

    set(${OUTPUT_LIST_VAR} ${${OUTPUT_LIST_VAR}} "${OBJ_PATH}" PARENT_SCOPE)
endfunction()

function(add_kernel_cpp_object SRC OUTPUT_LIST_VAR)
    string(REGEX REPLACE "\\.cpp$" ".o" OBJ_REL "${SRC}")
    set(OBJ_PATH "${OBJ_ROOT}/${OBJ_REL}")
    get_filename_component(OBJ_DIR "${OBJ_PATH}" DIRECTORY)
    set(_deps "${CMAKE_SOURCE_DIR}/${SRC}" ${KERNEL_INC_SOURCES_ABS})

    add_custom_command(
        OUTPUT "${OBJ_PATH}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${OBJ_DIR}"
        COMMAND ${KERNEL_CXX} ${CXXFLAGS_COMMON} ${ARCH_CFLAGS} -c "${CMAKE_SOURCE_DIR}/${SRC}" -o "${OBJ_PATH}"
        DEPENDS ${_deps}
        VERBATIM
    )

    set(${OUTPUT_LIST_VAR} ${${OUTPUT_LIST_VAR}} "${OBJ_PATH}" PARENT_SCOPE)
endfunction()

set(KERNEL_OBJECTS)
foreach(SRC IN LISTS C_SOURCES)
    add_kernel_c_object("${SRC}" KERNEL_OBJECTS)
endforeach()
foreach(SRC IN LISTS CPP_SOURCES)
    add_kernel_cpp_object("${SRC}" KERNEL_OBJECTS)
endforeach()
foreach(SRC IN LISTS ASM_SOURCES)
    add_kernel_asm_object("${SRC}" KERNEL_OBJECTS)
endforeach()

add_custom_command(
    OUTPUT "${KERNEL_RUST_LIB}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CLKS_BUILD_ROOT}"
    COMMAND ${RUSTC} --crate-type staticlib -C panic=abort -O "${CMAKE_SOURCE_DIR}/rust/src/lib.rs" -o "${KERNEL_RUST_LIB}"
    DEPENDS "${CMAKE_SOURCE_DIR}/rust/src/lib.rs"
    VERBATIM
)

add_custom_command(
    OUTPUT "${KERNEL_ELF}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CLKS_BUILD_ROOT}"
    COMMAND ${LD} ${LDFLAGS_COMMON} -T "${LINKER_SCRIPT}" -o "${KERNEL_ELF}" ${KERNEL_OBJECTS} "${KERNEL_RUST_LIB}"
    DEPENDS ${KERNEL_OBJECTS} "${LINKER_SCRIPT}" "${KERNEL_RUST_LIB}" "${CMAKE_SOURCE_DIR}/rust/src/lib.rs"
    VERBATIM
)

add_custom_target(kernel DEPENDS "${KERNEL_ELF}")

add_custom_command(
    OUTPUT "${KERNEL_SYMBOLS_FILE}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CLKS_BUILD_ROOT}"
    COMMAND ${CMAKE_COMMAND}
        "-DNM_TOOL=${NM}"
        "-DADDR2LINE_TOOL=${ADDR2LINE}"
        "-DKERNEL_ELF=${KERNEL_ELF}"
        "-DOUT_SYMBOL_FILE=${KERNEL_SYMBOLS_FILE}"
        -P "${CMAKE_SOURCE_DIR}/cmake/gen_kernel_symbols.cmake"
    DEPENDS "${KERNEL_ELF}" "${CMAKE_SOURCE_DIR}/cmake/gen_kernel_symbols.cmake"
    VERBATIM
)

add_custom_target(kernel-symbols DEPENDS "${KERNEL_SYMBOLS_FILE}")
add_dependencies(kernel-symbols kernel)

add_custom_target(setup-tools
    COMMAND ${CMAKE_COMMAND}
        "-DNO_COLOR=${NO_COLOR}"
        "-DCC_TOOL=${CC}"
        "-DLD_TOOL=${LD}"
        "-DOBJCOPY_TOOL=${OBJCOPY_FOR_TARGET}"
        "-DOBJDUMP_TOOL=${OBJDUMP_FOR_TARGET}"
        "-DREADELF_TOOL=${READELF_FOR_TARGET}"
        "-DNM_TOOL=${NM}"
        "-DADDR2LINE_TOOL=${ADDR2LINE}"
        "-DRUSTC_TOOL=${RUSTC}"
        "-DMAKE_TOOL=${MAKE_TOOL}"
        "-DSH_TOOL=${SH_TOOL}"
        -P "${CMAKE_SOURCE_DIR}/cmake/check_tools.cmake"
)

add_custom_target(setup DEPENDS setup-tools)
add_custom_command(
    TARGET setup
    POST_BUILD
    COMMAND ${CMAKE_COMMAND}
        "-DNO_COLOR=${NO_COLOR}"
        "-DLOG_LEVEL=INFO"
        "-DLOG_TEXT=environment ready"
        -P "${CL_LOG_EMIT_SCRIPT}"
)

find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    add_custom_target(menuconfig
        COMMAND ${Python3_EXECUTABLE} "${CMAKE_SOURCE_DIR}/scripts/menuconfig.py" --clks-only
        USES_TERMINAL
    )
    add_custom_target(menuconfig-gui
        COMMAND ${Python3_EXECUTABLE} "${CMAKE_SOURCE_DIR}/scripts/menuconfig.py" --gui --clks-only
        USES_TERMINAL
    )
else()
    add_custom_target(menuconfig
        COMMAND ${CMAKE_COMMAND} -E echo "python3 not found; run scripts/menuconfig.py --clks-only manually"
        USES_TERMINAL
    )
    add_custom_target(menuconfig-gui
        COMMAND ${CMAKE_COMMAND} -E echo "python3 not found; run scripts/menuconfig.py --gui --clks-only manually"
        USES_TERMINAL
    )
endif()

add_custom_target(clean-x86
    COMMAND ${CMAKE_COMMAND}
        "-DNO_COLOR=${NO_COLOR}"
        "-DLOG_LEVEL=STEP"
        "-DLOG_TEXT=cleaning ${CLKS_BUILD_ROOT}"
        -P "${CL_LOG_EMIT_SCRIPT}"
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${CLKS_BUILD_ROOT}"
    COMMAND ${CMAKE_COMMAND}
        "-DNO_COLOR=${NO_COLOR}"
        "-DLOG_LEVEL=INFO"
        "-DLOG_TEXT=clean done"
        -P "${CL_LOG_EMIT_SCRIPT}"
)

add_custom_target(clean-all
    COMMAND ${CMAKE_COMMAND}
        "-DNO_COLOR=${NO_COLOR}"
        "-DLOG_LEVEL=STEP"
        "-DLOG_TEXT=cleaning build"
        -P "${CL_LOG_EMIT_SCRIPT}"
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${CMAKE_SOURCE_DIR}/build"
    COMMAND ${CMAKE_COMMAND}
        "-DNO_COLOR=${NO_COLOR}"
        "-DLOG_LEVEL=INFO"
        "-DLOG_TEXT=clean-all done"
        -P "${CL_LOG_EMIT_SCRIPT}"
)

add_custom_target(clks-help
    COMMAND ${CMAKE_COMMAND} -E echo "CLKS CMake build system (x86_64 only)"
    COMMAND ${CMAKE_COMMAND} -E echo "  cmake -S . -B build-cmake"
    COMMAND ${CMAKE_COMMAND} -E echo "  cmake --build build-cmake --target setup"
    COMMAND ${CMAKE_COMMAND} -E echo "  cmake --build build-cmake --target menuconfig"
    COMMAND ${CMAKE_COMMAND} -E echo "  cmake --build build-cmake --target menuconfig-gui"
    COMMAND ${CMAKE_COMMAND} -E echo "  cmake --build build-cmake --target kernel"
    COMMAND ${CMAKE_COMMAND} -E echo "  cmake --build build-cmake --target kernel-symbols"
)

add_custom_target(clks-default ALL DEPENDS kernel)
