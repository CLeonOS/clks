use core::ffi::c_char;

use super::consts::*;
use super::{ffi, state, text, usercopy};

#[derive(Copy, Clone)]
struct Symbol {
    addr: u64,
    name: *const c_char,
    name_len: usize,
    source: *const c_char,
    source_len: usize,
}

#[repr(C)]
#[derive(Copy, Clone)]
struct KdbgBtReq {
    rbp: u64,
    rip: u64,
    out_ptr: u64,
    out_size: u64,
}

fn is_hex(ch: u8) -> bool {
    ch.is_ascii_hexdigit()
}

fn hex_value(ch: u8) -> u8 {
    if ch <= b'9' {
        ch - b'0'
    } else if ch <= b'F' {
        10 + (ch - b'A')
    } else {
        10 + (ch - b'a')
    }
}

unsafe fn parse_symbol_line(line: *const c_char, len: usize) -> Option<Symbol> {
    if line.is_null() || len == 0 {
        return None;
    }

    let mut i = 0usize;
    let mut addr = 0u64;
    let mut digits = 0usize;

    if len >= 2 {
        let a = *line as u8;
        let b = *line.add(1) as u8;
        if a == b'0' && (b == b'x' || b == b'X') {
            i = 2;
        }
    }

    while i < len && is_hex(*line.add(i) as u8) {
        addr = (addr << 4) | hex_value(*line.add(i) as u8) as u64;
        digits += 1;
        i += 1;
    }
    if digits == 0 {
        return None;
    }
    while i < len && matches!(*line.add(i) as u8, b' ' | b'\t') {
        i += 1;
    }
    if i >= len {
        return None;
    }
    let name_start = i;
    while i < len && !matches!(*line.add(i) as u8, b' ' | b'\t' | b'\r') {
        i += 1;
    }
    let name_end = i;
    if name_end <= name_start {
        return None;
    }
    while i < len && matches!(*line.add(i) as u8, b' ' | b'\t') {
        i += 1;
    }
    let source_start = i;
    let mut source_end = len;
    while source_end > source_start && matches!(*line.add(source_end - 1) as u8, b' ' | b'\t' | b'\r') {
        source_end -= 1;
    }

    Some(Symbol {
        addr,
        name: line.add(name_start),
        name_len: name_end - name_start,
        source: if source_end > source_start {
            line.add(source_start)
        } else {
            core::ptr::null()
        },
        source_len: if source_end > source_start {
            source_end - source_start
        } else {
            0
        },
    })
}

fn symbols_ready() -> bool {
    unsafe {
        if state::SYMBOLS_CHECKED != 0 {
            return !state::SYMBOLS_DATA.is_null() && state::SYMBOLS_SIZE > 0;
        }
        state::SYMBOLS_CHECKED = CLKS_TRUE;
        if ffi::clks_fs_is_ready() == 0 {
            return false;
        }
        let mut size = 0u64;
        let data = ffi::clks_fs_read_all(KERNEL_SYMBOL_FILE.as_ptr() as *const i8, &mut size as *mut u64);
        if data.is_null() || size == 0 {
            return false;
        }
        state::SYMBOLS_DATA = data as *const i8;
        state::SYMBOLS_SIZE = size;
        true
    }
}

fn lookup_symbol(addr: u64) -> Option<Symbol> {
    if !symbols_ready() {
        return None;
    }

    unsafe {
        let mut data = state::SYMBOLS_DATA;
        let end = state::SYMBOLS_DATA.add(state::SYMBOLS_SIZE as usize);
        let mut best: Option<Symbol> = None;

        while data < end {
            let line = data;
            let mut line_len = 0usize;
            while data < end && *data as u8 != b'\n' {
                data = data.add(1);
                line_len += 1;
            }
            if data < end && *data as u8 == b'\n' {
                data = data.add(1);
            }
            if let Some(sym) = parse_symbol_line(line, line_len) {
                if sym.addr <= addr && best.map(|b| sym.addr >= b.addr).unwrap_or(true) {
                    best = Some(sym);
                }
            }
        }
        best
    }
}

fn format_symbol_into(out: &mut [i8], mut pos: usize, addr: u64) -> usize {
    pos = text::append_u64_hex(out, pos, addr);
    if let Some(sym) = lookup_symbol(addr) {
        pos = text::append_char(out, pos, b' ');
        pos = text::append_c_n(out, pos, sym.name, sym.name_len);
        pos = text::append_char(out, pos, b'+');
        pos = text::append_u64_hex(out, pos, addr - sym.addr);
        if !sym.source.is_null() && sym.source_len > 0 {
            pos = text::append_bytes(out, pos, b" @ ");
            pos = text::append_c_n(out, pos, sym.source, sym.source_len);
        }
    } else {
        pos = text::append_bytes(out, pos, b" <no-symbol>");
    }
    pos
}

fn append_bt_frame(out: &mut [i8], mut pos: usize, index: u64, rip: u64) -> usize {
    pos = text::append_char(out, pos, b'#');
    pos = text::append_u64_dec(out, pos, index);
    pos = text::append_char(out, pos, b' ');
    pos = format_symbol_into(out, pos, rip);
    text::append_char(out, pos, b'\n')
}

fn stack_ptr_valid(ptr: u64, stack_low: u64, stack_high: u64) -> bool {
    (ptr & 0x7) == 0 && ptr >= stack_low && ptr.checked_add(16).is_some_and(|v| v <= stack_high) && ptr >= KERNEL_ADDR_BASE
}

pub fn sym(arg0: u64, arg1: u64, arg2: u64) -> u64 {
    if arg1 == 0 || arg2 == 0 {
        return 0;
    }
    let mut out = [0i8; KDBG_TEXT_MAX];
    let len = format_symbol_into(&mut out, 0, arg0);
    usercopy::copy_text_to_user(arg1, arg2, out.as_ptr(), len)
}

pub fn regs(arg0: u64, arg1: u64) -> u64 {
    if arg0 == 0 || arg1 == 0 {
        return 0;
    }
    let mut out = [0i8; KDBG_TEXT_MAX];
    let mut pos = 0usize;
    unsafe {
        if state::LAST_FRAME_VALID == 0 {
            pos = text::append_bytes(&mut out, pos, b"NO REG SNAPSHOT\n");
            return usercopy::copy_text_to_user(arg0, arg1, out.as_ptr(), pos);
        }
        let frame = state::LAST_FRAME;
        macro_rules! reg {
            ($name:expr, $value:expr) => {{
                pos = text::append_bytes(&mut out, pos, $name);
                pos = text::append_u64_hex(&mut out, pos, $value);
            }};
        }
        reg!(b"RAX=", frame.rax);
        reg!(b" RBX=", frame.rbx);
        reg!(b" RCX=", frame.rcx);
        reg!(b" RDX=", frame.rdx);
        pos = text::append_char(&mut out, pos, b'\n');
        reg!(b"RSI=", frame.rsi);
        reg!(b" RDI=", frame.rdi);
        reg!(b" RBP=", frame.rbp);
        reg!(b" RSP=", frame.rsp);
        pos = text::append_char(&mut out, pos, b'\n');
        reg!(b"R8 =", frame.r8);
        reg!(b" R9 =", frame.r9);
        reg!(b" R10=", frame.r10);
        reg!(b" R11=", frame.r11);
        pos = text::append_char(&mut out, pos, b'\n');
        reg!(b"R12=", frame.r12);
        reg!(b" R13=", frame.r13);
        reg!(b" R14=", frame.r14);
        reg!(b" R15=", frame.r15);
        pos = text::append_char(&mut out, pos, b'\n');
        reg!(b"RIP=", frame.rip);
        reg!(b" CS=", frame.cs);
        reg!(b" RFLAGS=", frame.rflags);
        pos = text::append_char(&mut out, pos, b'\n');
        reg!(b"VECTOR=", frame.vector);
        reg!(b" ERROR=", frame.error_code);
        reg!(b" SS=", frame.ss);
        pos = text::append_char(&mut out, pos, b'\n');
    }
    usercopy::copy_text_to_user(arg0, arg1, out.as_ptr(), pos)
}

pub fn bt(arg0: u64) -> u64 {
    let Some(req) = usercopy::copy_from_user_struct::<KdbgBtReq>(arg0) else {
        return 0;
    };
    if req.out_ptr == 0 || req.out_size == 0 {
        return 0;
    }

    let mut out = [0i8; KDBG_TEXT_MAX];
    let mut pos = text::append_bytes(&mut out, 0, b"BT RBP=");
    pos = text::append_u64_hex(&mut out, pos, req.rbp);
    pos = text::append_bytes(&mut out, pos, b" RIP=");
    pos = text::append_u64_hex(&mut out, pos, req.rip);
    pos = text::append_char(&mut out, pos, b'\n');
    let mut frame_index = 0u64;

    if req.rip != 0 {
        pos = append_bt_frame(&mut out, pos, frame_index, req.rip);
        frame_index += 1;
    }

    #[cfg(target_arch = "x86_64")]
    unsafe {
        let current_rsp: u64;
        core::arch::asm!("mov {}, rsp", out(reg) current_rsp, options(nomem, nostack, preserves_flags));
        let mut current_rbp = req.rbp;
        let mut stack_low = if current_rsp > KDBG_STACK_WINDOW_BYTES {
            current_rsp - KDBG_STACK_WINDOW_BYTES
        } else {
            KERNEL_ADDR_BASE
        };
        let mut stack_high = current_rsp.wrapping_add(KDBG_STACK_WINDOW_BYTES);
        if stack_high < current_rsp {
            stack_high = u64::MAX;
        }
        if stack_low < KERNEL_ADDR_BASE {
            stack_low = KERNEL_ADDR_BASE;
        }

        if stack_ptr_valid(current_rbp, stack_low, stack_high) {
            while frame_index < KDBG_BT_MAX_FRAMES as u64 {
                let frame_ptr = current_rbp as *const u64;
                let next_rbp = *frame_ptr;
                let ret_rip = *frame_ptr.add(1);
                if ret_rip == 0 {
                    break;
                }
                pos = append_bt_frame(&mut out, pos, frame_index, ret_rip);
                frame_index += 1;
                if next_rbp <= current_rbp || !stack_ptr_valid(next_rbp, stack_low, stack_high) {
                    break;
                }
                current_rbp = next_rbp;
            }
        } else {
            pos = text::append_bytes(
                &mut out,
                pos,
                b"NOTE: stack walk skipped (rbp not in current kernel stack window)\n",
            );
        }
    }

    #[cfg(not(target_arch = "x86_64"))]
    {
        pos = text::append_bytes(&mut out, pos, b"NOTE: stack walk unsupported on this arch\n");
    }

    usercopy::copy_text_to_user(req.out_ptr, req.out_size, out.as_ptr(), pos)
}
