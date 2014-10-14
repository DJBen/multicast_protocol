#include "net_include.h"

int main(int argc, char const *argv[])
{
    struct sockaddr_in name;
    struct sockaddr_in send_addr;

    struct ip_mreq     mreq;
    unsigned char      ttl_val;

    int                ss;
    int                bytes;
    packet             *start_sig;

    start_sig = malloc(sizeof(packet));
    start_sig->type = PACKET_TYPE_START;
    ss = socket(AF_INET, SOCK_DGRAM, 0); /* Socket for sending */
    if (ss<0) {
        perror("Mcast: socket");
        exit(1);
    }

    ttl_val = 1;
    if (setsockopt(ss, IPPROTO_IP, IP_MULTICAST_TTL, (void *)&ttl_val,
        sizeof(ttl_val)) < 0)
    {
        printf("Mcast: problem in setsockopt of multicast ttl %d - ignore in WinNT or Win95\n", ttl_val );
    }

    send_addr.sin_family = AF_INET;
    send_addr.sin_addr.s_addr = htonl(MCAST_ADDR);  /* mcast address */
    send_addr.sin_port = htons(PORT);

    sendto( ss, start_sig, sizeof(start_sig), 0,
        (struct sockaddr *)&send_addr, sizeof(send_addr) );

    return 0;
}
