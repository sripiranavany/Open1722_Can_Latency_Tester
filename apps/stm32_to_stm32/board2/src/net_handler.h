#ifndef NET_HANDLER_H
#define NET_HANDLER_H

#include <zephyr/posix/sys/socket.h>
#include <zephyr/net/ethernet.h>

int net_handler_init(int *sock_out, struct sockaddr_ll *sk_addr_out);

#endif
