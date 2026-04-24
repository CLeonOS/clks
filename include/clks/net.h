#ifndef CLKS_NET_H
#define CLKS_NET_H

#include <clks/types.h>

void clks_net_init(void);
void clks_net_poll(void);

clks_bool clks_net_available(void);
u32 clks_net_ipv4_addr_be(void);

clks_bool clks_net_ping_ipv4(u32 dst_ipv4_be, u64 poll_budget);
u64 clks_net_udp_send(u32 dst_ipv4_be, u16 dst_port, u16 src_port, const void *payload, u64 payload_len);
u64 clks_net_udp_recv(void *out_payload, u64 payload_capacity, u32 *out_src_ipv4_be, u16 *out_src_port,
                      u16 *out_dst_port);

#endif
