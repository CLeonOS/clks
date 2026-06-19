use core::{ffi::c_void, ptr};

use crate::syscall::consts::*;
use crate::syscall::{ffi, usercopy};

#[repr(C)]
#[derive(Copy, Clone)]
struct UdpSendReq {
    dst_ipv4_be: u64,
    dst_port: u64,
    src_port: u64,
    payload_ptr: u64,
    payload_len: u64,
}

#[repr(C)]
#[derive(Copy, Clone)]
struct UdpRecvReq {
    out_payload_ptr: u64,
    payload_capacity: u64,
    out_src_ipv4_ptr: u64,
    out_src_port_ptr: u64,
    out_dst_port_ptr: u64,
}

#[repr(C)]
#[derive(Copy, Clone)]
struct TcpConnectReq {
    dst_ipv4_be: u64,
    dst_port: u64,
    src_port: u64,
    poll_budget: u64,
}

#[repr(C)]
#[derive(Copy, Clone)]
struct TcpListenReq {
    port: u64,
}

#[repr(C)]
#[derive(Copy, Clone)]
struct TcpAcceptReq {
    poll_budget: u64,
}

#[repr(C)]
#[derive(Copy, Clone)]
struct TcpSendReq {
    payload_ptr: u64,
    payload_len: u64,
    poll_budget: u64,
}

#[repr(C)]
#[derive(Copy, Clone)]
struct TcpRecvReq {
    out_payload_ptr: u64,
    payload_capacity: u64,
    poll_budget: u64,
}

pub fn available() -> u64 {
    if unsafe { ffi::clks_net_available() } != 0 { 1 } else { 0 }
}
pub fn ipv4_addr() -> u64 {
    unsafe { ffi::clks_net_ipv4_addr_be() as u64 }
}
pub fn netmask() -> u64 {
    unsafe { ffi::clks_net_ipv4_netmask_be() as u64 }
}
pub fn gateway() -> u64 {
    unsafe { ffi::clks_net_ipv4_gateway_be() as u64 }
}
pub fn dns_server() -> u64 {
    unsafe { ffi::clks_net_ipv4_dns_be() as u64 }
}
pub fn ping(arg0: u64, arg1: u64) -> u64 {
    if unsafe { ffi::clks_net_ping_ipv4(arg0 as u32, arg1) } != 0 { 1 } else { 0 }
}

pub fn udp_send(arg0: u64) -> u64 {
    let Some(req) = usercopy::copy_from_user_struct::<UdpSendReq>(arg0) else {
        return 0;
    };
    if req.payload_len > NET_UDP_PAYLOAD_MAX as u64 {
        return 0;
    }
    let mut payload_copy: *mut c_void = core::ptr::null_mut();
    if req.payload_len > 0 {
        if req.payload_ptr == 0 || !usercopy::user_ptr_readable(req.payload_ptr, req.payload_len) {
            return 0;
        }
        payload_copy = unsafe { ffi::clks_kmalloc(req.payload_len as usize) };
        if payload_copy.is_null() {
            return 0;
        }
        unsafe {
            ptr::copy_nonoverlapping(req.payload_ptr as *const u8, payload_copy as *mut u8, req.payload_len as usize);
        }
    }
    let sent = unsafe {
        ffi::clks_net_udp_send(
            req.dst_ipv4_be as u32,
            req.dst_port as u16,
            req.src_port as u16,
            payload_copy as *const c_void,
            req.payload_len,
        )
    };
    if !payload_copy.is_null() {
        unsafe { ffi::clks_kfree(payload_copy) };
    }
    sent
}

pub fn udp_recv(arg0: u64) -> u64 {
    let Some(req) = usercopy::copy_from_user_struct::<UdpRecvReq>(arg0) else {
        return 0;
    };
    if req.out_payload_ptr == 0 || req.payload_capacity == 0 {
        return 0;
    }
    let mut capacity = req.payload_capacity;
    if capacity > NET_UDP_PAYLOAD_MAX as u64 {
        capacity = NET_UDP_PAYLOAD_MAX as u64;
    }
    if !usercopy::user_ptr_writable(req.out_payload_ptr, capacity) {
        return 0;
    }
    if req.out_src_ipv4_ptr != 0 && !usercopy::user_ptr_writable(req.out_src_ipv4_ptr, 8) {
        return 0;
    }
    if req.out_src_port_ptr != 0 && !usercopy::user_ptr_writable(req.out_src_port_ptr, 8) {
        return 0;
    }
    if req.out_dst_port_ptr != 0 && !usercopy::user_ptr_writable(req.out_dst_port_ptr, 8) {
        return 0;
    }
    let mut packet = [0u8; NET_UDP_PAYLOAD_MAX];
    let mut src_ipv4 = 0u32;
    let mut src_port = 0u16;
    let mut dst_port = 0u16;
    let got = unsafe {
        ffi::clks_net_udp_recv(
            packet.as_mut_ptr() as *mut c_void,
            capacity,
            &mut src_ipv4 as *mut u32,
            &mut src_port as *mut u16,
            &mut dst_port as *mut u16,
        )
    };
    if got == 0 {
        return 0;
    }
    unsafe {
        ptr::copy_nonoverlapping(packet.as_ptr(), req.out_payload_ptr as *mut u8, got as usize);
        if req.out_src_ipv4_ptr != 0 {
            *(req.out_src_ipv4_ptr as *mut u64) = src_ipv4 as u64;
        }
        if req.out_src_port_ptr != 0 {
            *(req.out_src_port_ptr as *mut u64) = src_port as u64;
        }
        if req.out_dst_port_ptr != 0 {
            *(req.out_dst_port_ptr as *mut u64) = dst_port as u64;
        }
    }
    got
}

pub fn tcp_connect(arg0: u64) -> u64 {
    let Some(req) = usercopy::copy_from_user_struct::<TcpConnectReq>(arg0) else {
        return 0;
    };
    if unsafe { ffi::clks_net_tcp_connect(req.dst_ipv4_be as u32, req.dst_port as u16, req.src_port as u16, req.poll_budget) } != 0 {
        1
    } else {
        0
    }
}

pub fn tcp_listen(arg0: u64) -> u64 {
    let Some(req) = usercopy::copy_from_user_struct::<TcpListenReq>(arg0) else {
        return 0;
    };
    if unsafe { ffi::clks_net_tcp_listen(req.port as u16) } != 0 { 1 } else { 0 }
}

pub fn tcp_accept(arg0: u64) -> u64 {
    let Some(req) = usercopy::copy_from_user_struct::<TcpAcceptReq>(arg0) else {
        return 0;
    };
    if unsafe { ffi::clks_net_tcp_accept(req.poll_budget) } != 0 { 1 } else { 0 }
}

pub fn tcp_send(arg0: u64) -> u64 {
    let Some(req) = usercopy::copy_from_user_struct::<TcpSendReq>(arg0) else {
        return 0;
    };
    if req.payload_len == 0 || req.payload_len > NET_TCP_IO_MAX as u64 {
        return 0;
    }
    if req.payload_ptr == 0 || !usercopy::user_ptr_readable(req.payload_ptr, req.payload_len) {
        return 0;
    }
    let payload_copy = unsafe { ffi::clks_kmalloc(req.payload_len as usize) };
    if payload_copy.is_null() {
        return 0;
    }
    unsafe {
        ptr::copy_nonoverlapping(req.payload_ptr as *const u8, payload_copy as *mut u8, req.payload_len as usize);
    }
    let sent = unsafe { ffi::clks_net_tcp_send(payload_copy as *const c_void, req.payload_len, req.poll_budget) };
    unsafe { ffi::clks_kfree(payload_copy) };
    sent
}

pub fn tcp_recv(arg0: u64) -> u64 {
    let Some(req) = usercopy::copy_from_user_struct::<TcpRecvReq>(arg0) else {
        return 0;
    };
    if req.out_payload_ptr == 0 || req.payload_capacity == 0 {
        return 0;
    }
    let mut capacity = req.payload_capacity;
    if capacity > NET_TCP_IO_MAX as u64 {
        capacity = NET_TCP_IO_MAX as u64;
    }
    if !usercopy::user_ptr_writable(req.out_payload_ptr, capacity) {
        return 0;
    }
    let mut packet = [0u8; NET_TCP_IO_MAX];
    let got = unsafe { ffi::clks_net_tcp_recv(packet.as_mut_ptr() as *mut c_void, capacity, req.poll_budget) };
    if got == 0 {
        return 0;
    }
    unsafe {
        ptr::copy_nonoverlapping(packet.as_ptr(), req.out_payload_ptr as *mut u8, got as usize);
    }
    got
}

pub fn tcp_close(arg0: u64) -> u64 {
    if unsafe { ffi::clks_net_tcp_close(arg0) } != 0 { 1 } else { 0 }
}

pub fn tcp_last_error() -> u64 {
    unsafe { ffi::clks_net_tcp_last_error() }
}
