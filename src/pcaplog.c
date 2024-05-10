

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>

#include "settings.h"
#include "pcaplog.h"
#include "pcap_dump.h"
#include "mutex_manager.h"
#include "cpu_endian.h"

static FsFile *pcap = NULL;

void pcaplog_open()
{
    if (!settings_get_bool("pcap.enabled"))
    {
        return;
    }

    const char *filename = settings_get_string("pcap.filename");

    TRACE_WARNING("pcap dump is enabled - this will cause the file '%s' to grow indefinitely!\r\n", filename);

    pcap = pd_create(filename, 101, 0, 65535);
    if (!pcap)
    {
        fprintf(stderr, "Error opening pcap file\n");
        return;
    }
}

void pcaplog_close()
{
    if (!pcap)
    {
        return;
    }

    pd_close(pcap);

    pcap = 0;
}

void pcaplog_write(pcaplog_ctx_t *ctx, bool is_tx, const uint8_t *payload, size_t payload_len)
{
    if (!pcap || !payload_len || !ctx)
    {
        return;
    }

    pcaplog_endpoint_t src = is_tx ? ctx->local_endpoint : ctx->remote_endpoint;
    pcaplog_endpoint_t dst = !is_tx ? ctx->local_endpoint : ctx->remote_endpoint;

    size_t packet_len = sizeof(pcaplog_ip_t) + sizeof(pcaplog_tcphdr_t) + payload_len;
    uint8_t *packet = malloc(packet_len);

    pcaplog_ip_t ip_header;
    ip_header.ip_hl = 5;
    ip_header.ip_v = 4;
    ip_header.ip_tos = 0;
    ip_header.ip_len = htons(packet_len);
    ip_header.ip_id = 0;
    ip_header.ip_off = 0;
    ip_header.ip_ttl = 64;
    ip_header.ip_p = 6; // IPPROTO_TCP;
    ip_header.ip_sum = 0;
    ip_header.ip_src = src.ipv4;
    ip_header.ip_dst = dst.ipv4;

    pcaplog_tcphdr_t tcp_header;
    tcp_header.th_sport = htons(src.port);
    tcp_header.th_dport = htons(dst.port);
    tcp_header.th_ack = htonl(!is_tx ? ctx->pcap_data->seq_tx : ctx->pcap_data->seq_rx);
    tcp_header.th_seq = htonl(is_tx ? ctx->pcap_data->seq_tx : ctx->pcap_data->seq_rx);
    tcp_header.th_x2 = 0;
    tcp_header.th_off = 5;
    tcp_header.th_flags = TH_ACK;
    tcp_header.th_win = htons(65535);
    tcp_header.th_sum = 0;
    tcp_header.th_urp = 0;
    tcp_header.th_sum = 0xFFFF;

    if (is_tx)
    {
        ctx->pcap_data->seq_tx += payload_len;
    }
    else
    {
        ctx->pcap_data->seq_rx += payload_len;
    }

    memcpy(packet, &ip_header, sizeof(ip_header));
    memcpy(packet + sizeof(ip_header), &tcp_header, sizeof(tcp_header));
    memcpy(packet + sizeof(ip_header) + sizeof(tcp_header), payload, payload_len);

    mutex_lock(MUTEX_PCAPLOG_FILE);
    pd_write(pcap, packet, packet_len);
    mutex_unlock(MUTEX_PCAPLOG_FILE);

    free(packet);
}