static u64 clks_syscall_net_available(void) {
    return (clks_net_available() == CLKS_TRUE) ? 1ULL : 0ULL;
}

static u64 clks_syscall_net_ipv4_addr(void) {
    return (u64)clks_net_ipv4_addr_be();
}

static u64 clks_syscall_net_netmask(void) {
    return (u64)clks_net_ipv4_netmask_be();
}

static u64 clks_syscall_net_gateway(void) {
    return (u64)clks_net_ipv4_gateway_be();
}

static u64 clks_syscall_net_dns_server(void) {
    return (u64)clks_net_ipv4_dns_be();
}

static u64 clks_syscall_net_ping(u64 arg0, u64 arg1) {
    return (clks_net_ping_ipv4((u32)arg0, arg1) == CLKS_TRUE) ? 1ULL : 0ULL;
}

static u64 clks_syscall_net_udp_send(u64 arg0) {
    struct clks_syscall_net_udp_send_req req;
    void *payload_copy = CLKS_NULL;
    u64 payload_len;
    u64 sent = 0ULL;

    if (arg0 == 0ULL) {
        return 0ULL;
    }

    if (clks_syscall_user_ptr_readable(arg0, (u64)sizeof(req)) == CLKS_FALSE) {
        return 0ULL;
    }

    clks_memcpy(&req, (const void *)arg0, sizeof(req));

    payload_len = req.payload_len;
    if (payload_len > (u64)CLKS_SYSCALL_NET_UDP_PAYLOAD_MAX) {
        return 0ULL;
    }

    if (payload_len > 0ULL) {
        if (req.payload_ptr == 0ULL) {
            return 0ULL;
        }

        if (clks_syscall_user_ptr_readable(req.payload_ptr, payload_len) == CLKS_FALSE) {
            return 0ULL;
        }

        payload_copy = clks_kmalloc((usize)payload_len);
        if (payload_copy == CLKS_NULL) {
            return 0ULL;
        }

        clks_memcpy(payload_copy, (const void *)req.payload_ptr, (usize)payload_len);
    }

    sent = clks_net_udp_send((u32)req.dst_ipv4_be, (u16)req.dst_port, (u16)req.src_port, payload_copy, payload_len);

    if (payload_copy != CLKS_NULL) {
        clks_kfree(payload_copy);
    }

    return sent;
}

static u64 clks_syscall_net_udp_recv(u64 arg0) {
    struct clks_syscall_net_udp_recv_req req;
    u8 packet[CLKS_SYSCALL_NET_UDP_PAYLOAD_MAX];
    u64 capacity;
    u64 got;
    u32 src_ipv4 = 0U;
    u16 src_port = 0U;
    u16 dst_port = 0U;

    if (arg0 == 0ULL) {
        return 0ULL;
    }

    if (clks_syscall_user_ptr_readable(arg0, (u64)sizeof(req)) == CLKS_FALSE) {
        return 0ULL;
    }

    clks_memcpy(&req, (const void *)arg0, sizeof(req));

    if (req.out_payload_ptr == 0ULL || req.payload_capacity == 0ULL) {
        return 0ULL;
    }

    capacity = req.payload_capacity;
    if (capacity > (u64)sizeof(packet)) {
        capacity = (u64)sizeof(packet);
    }

    if (clks_syscall_user_ptr_writable(req.out_payload_ptr, capacity) == CLKS_FALSE) {
        return 0ULL;
    }

    if (req.out_src_ipv4_ptr != 0ULL &&
        clks_syscall_user_ptr_writable(req.out_src_ipv4_ptr, (u64)sizeof(u64)) == CLKS_FALSE) {
        return 0ULL;
    }

    if (req.out_src_port_ptr != 0ULL &&
        clks_syscall_user_ptr_writable(req.out_src_port_ptr, (u64)sizeof(u64)) == CLKS_FALSE) {
        return 0ULL;
    }

    if (req.out_dst_port_ptr != 0ULL &&
        clks_syscall_user_ptr_writable(req.out_dst_port_ptr, (u64)sizeof(u64)) == CLKS_FALSE) {
        return 0ULL;
    }

    got = clks_net_udp_recv(packet, capacity, &src_ipv4, &src_port, &dst_port);
    if (got == 0ULL) {
        return 0ULL;
    }

    clks_memcpy((void *)req.out_payload_ptr, packet, (usize)got);

    if (req.out_src_ipv4_ptr != 0ULL) {
        *((u64 *)(usize)req.out_src_ipv4_ptr) = (u64)src_ipv4;
    }

    if (req.out_src_port_ptr != 0ULL) {
        *((u64 *)(usize)req.out_src_port_ptr) = (u64)src_port;
    }

    if (req.out_dst_port_ptr != 0ULL) {
        *((u64 *)(usize)req.out_dst_port_ptr) = (u64)dst_port;
    }

    return got;
}

static u64 clks_syscall_net_tcp_connect(u64 arg0) {
    struct clks_syscall_net_tcp_connect_req req;

    if (arg0 == 0ULL) {
        return 0ULL;
    }

    if (clks_syscall_user_ptr_readable(arg0, (u64)sizeof(req)) == CLKS_FALSE) {
        return 0ULL;
    }

    clks_memcpy(&req, (const void *)arg0, sizeof(req));
    return (clks_net_tcp_connect((u32)req.dst_ipv4_be, (u16)req.dst_port, (u16)req.src_port, req.poll_budget) ==
            CLKS_TRUE)
               ? 1ULL
               : 0ULL;
}

static u64 clks_syscall_net_tcp_send(u64 arg0) {
    struct clks_syscall_net_tcp_send_req req;
    void *payload_copy = CLKS_NULL;
    u64 payload_len;
    u64 sent = 0ULL;

    if (arg0 == 0ULL) {
        return 0ULL;
    }

    if (clks_syscall_user_ptr_readable(arg0, (u64)sizeof(req)) == CLKS_FALSE) {
        return 0ULL;
    }

    clks_memcpy(&req, (const void *)arg0, sizeof(req));
    payload_len = req.payload_len;
    if (payload_len == 0ULL || payload_len > CLKS_SYSCALL_NET_TCP_IO_MAX) {
        return 0ULL;
    }

    if (req.payload_ptr == 0ULL || clks_syscall_user_ptr_readable(req.payload_ptr, payload_len) == CLKS_FALSE) {
        return 0ULL;
    }

    payload_copy = clks_kmalloc((usize)payload_len);
    if (payload_copy == CLKS_NULL) {
        return 0ULL;
    }

    clks_memcpy(payload_copy, (const void *)req.payload_ptr, (usize)payload_len);
    sent = clks_net_tcp_send(payload_copy, payload_len, req.poll_budget);
    clks_kfree(payload_copy);
    return sent;
}

static u64 clks_syscall_net_tcp_recv(u64 arg0) {
    struct clks_syscall_net_tcp_recv_req req;
    u8 packet[CLKS_SYSCALL_NET_TCP_IO_MAX];
    u64 capacity;
    u64 got;

    if (arg0 == 0ULL) {
        return 0ULL;
    }

    if (clks_syscall_user_ptr_readable(arg0, (u64)sizeof(req)) == CLKS_FALSE) {
        return 0ULL;
    }

    clks_memcpy(&req, (const void *)arg0, sizeof(req));
    if (req.out_payload_ptr == 0ULL || req.payload_capacity == 0ULL) {
        return 0ULL;
    }

    capacity = req.payload_capacity;
    if (capacity > (u64)sizeof(packet)) {
        capacity = (u64)sizeof(packet);
    }

    if (clks_syscall_user_ptr_writable(req.out_payload_ptr, capacity) == CLKS_FALSE) {
        return 0ULL;
    }

    got = clks_net_tcp_recv(packet, capacity, req.poll_budget);
    if (got == 0ULL) {
        return 0ULL;
    }

    clks_memcpy((void *)req.out_payload_ptr, packet, (usize)got);
    return got;
}

static u64 clks_syscall_net_tcp_close(u64 arg0) {
    return (clks_net_tcp_close(arg0) == CLKS_TRUE) ? 1ULL : 0ULL;
}

static u64 clks_syscall_net_tcp_last_error(void) {
    return clks_net_tcp_last_error();
}

