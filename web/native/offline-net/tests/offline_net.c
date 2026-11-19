#include "host/net/net.h"
#include "runtime/net/net_session.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static int unexpected_server_setting(char *text, int size)
{
    (void)text;
    (void)size;
    assert(!"offline startup requested a server");
    return 0;
}

int main(void)
{
    const unsigned char id[8] = {1};
    KhdaysNetEvent event = {0};
    unsigned char byte = 0x5a;
    KhdaysNetAddress address = {23, 45};
    unsigned int local = 64;

    khdays_net_settings_source(unexpected_server_setting);
    for (int i = 0; i < 2; ++i) {
        assert(!khdays_net_in_session());
        assert(!khdays_net_begin());
        khdays_net_scan(1, 1);
        assert(!khdays_net_host(1, &byte, 1, 1));
        assert(!khdays_net_join(id, &byte, 1));
        assert(!khdays_net_send(1, 1, &byte, 1));
        assert(!khdays_net_pending());
        assert(!khdays_net_next(&event, &byte, 1, 1));
        assert(khdays_net_own_aid() == -1);
        assert(!khdays_net_room_count());
        assert(!khdays_net_in_session());
        khdays_net_end();
    }

    assert(khdays_host_udp_open(0, 0) == NULL);
    assert(!khdays_host_udp_join(NULL, 0));
    assert(!khdays_host_udp_send(NULL, &address, &byte, 1));
    assert(khdays_host_udp_receive(NULL, &address, &byte, 1) == -1);
    assert(!khdays_host_net_addresses(&local, 1));
    assert(!khdays_host_net_resolve("localhost", 1, &address));
    assert(khdays_host_tcp_exchange(&address, &byte, 1, &byte, 1, 1, &local) == -1);
    assert(!khdays_host_net_gateway(&local));
    assert(address.ip == 23 && address.port == 45 && local == 64 && byte == 0x5a);

    const unsigned long long first = khdays_host_net_clock_ms();
    unsigned char random[514];
    memset(random, 0, sizeof(random));
    random[0] = 0x5a;
    random[513] = 0xa5;
    khdays_host_random(random + 1, 512);
    assert(random[0] == 0x5a && random[513] == 0xa5);
    for (size_t offset = 1; offset < 513; offset += 256) {
        unsigned char any = 0;
        for (size_t i = 0; i < 256; ++i) {
            any |= random[offset + i];
        }
        assert(any != 0);
    }
    assert(khdays_host_net_clock_ms() >= first);
    puts("Native wireless session stays offline through startup, rejected operations, and reset.");
    return 0;
}
