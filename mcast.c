#include "net_include.h"

#define NAME_LENGTH 80

int gethostname(char*,size_t);
void waitForStart(int number_of_packets, int machine_index, int number_of_machines, int loss_rate_percent, int *sr);
void prepare(int number_of_packets, int machine_index, int number_of_machines, int loss_rate_percent, int sr, int *ss);
void transmit(int number_of_packets, int machine_index, int number_of_machines, int loss_rate_percent, int sr, int ss);

int buildPrepPacket(packet **prep, int machine_index, char *my_host_name);
bool parsePrepPacket(packet *prep, char *next_host_name, int machine_index, int number_of_machines);

int main(int argc, char const *argv[])
{
    int loss_rate_percent;
    int machine_index;
    int number_of_machines;
    int number_of_packets;

    int sr, ss;

    if (argc != 5) {
        printf("Usage: mcast <num_of_packets> <machine_index> <number of machines> <loss rate>\n");
        return 1;
    }
    number_of_packets = (int)strtol(argv[1], (char **)NULL, 10);
    machine_index = (int)strtol(argv[2], (char **)NULL, 10);
    number_of_machines = (int)strtol(argv[3], (char **)NULL, 10);
    loss_rate_percent = (int)strtol(argv[4], (char **)NULL, 10);

    /* Some sanity checking to make sure that the number of machines is larger than machine index */
    if (number_of_machines <= machine_index) {
        printf("Error: number of machines should be larger than this machine's index.\n");
        return 2;
    }

    printf("%d (%d/%d) %d\n", number_of_packets, machine_index, number_of_machines, loss_rate_percent);
    waitForStart(number_of_packets, machine_index, number_of_machines, loss_rate_percent, &sr);
    prepare(number_of_packets, machine_index, number_of_machines, loss_rate_percent, sr, &ss);
    transmit(number_of_packets, machine_index, number_of_machines, loss_rate_percent, sr, ss);

    return 0;
}

void waitForStart(int number_of_packets, int machine_index, int number_of_machines, int loss_rate_percent, int *sr) {

    struct sockaddr_in name;

    struct ip_mreq     mreq;

    int                socket_r;
    fd_set             mask;
    fd_set             dummy_mask,temp_mask;
    int                bytes;
    int                num;
    packet *start_sig;

    start_sig = malloc(sizeof(packet));
    *sr = socket(AF_INET, SOCK_DGRAM, 0); /* socket for receiving */
    socket_r = *sr;
    if (socket_r<0) {
        perror("Mcast: socket");
        exit(1);
    }

    name.sin_family = AF_INET;
    name.sin_addr.s_addr = INADDR_ANY;
    name.sin_port = htons(PORT);

    if ( bind( socket_r, (struct sockaddr *)&name, sizeof(name) ) < 0 ) {
        perror("Mcast: bind");
        exit(1);
    }

    mreq.imr_multiaddr.s_addr = htonl( MCAST_ADDR );

    /* the interface could be changed to a specific interface if needed */
    mreq.imr_interface.s_addr = htonl( INADDR_ANY );

    if (setsockopt(socket_r, IPPROTO_IP, IP_ADD_MEMBERSHIP, (void *)&mreq,
        sizeof(mreq)) < 0)
    {
        perror("Mcast: problem in setsockopt to join multicast address" );
    }

    FD_ZERO( &mask );
    FD_ZERO( &dummy_mask );
    FD_SET( socket_r, &mask );
    for(;;)
    {
        temp_mask = mask;
        num = select( FD_SETSIZE, &temp_mask, &dummy_mask, &dummy_mask, NULL);
        if (num > 0) {
            if ( FD_ISSET( socket_r, &temp_mask) ) {
                bytes = recv( socket_r, start_sig, sizeof(packet), 0 );
                if (bytes > 0) {
                    if (start_sig->type == PACKET_TYPE_START) {
                        free(start_sig);
                        break;
                    }
                }
            }
        }
    }
}

void prepare(int number_of_packets, int machine_index, int number_of_machines, int loss_rate_percent, int sr, int *ss) {

    struct sockaddr_in send_addr;

    unsigned char      ttl_val;

    int                socket_s;
    fd_set             mask;
    fd_set             dummy_mask,temp_mask;
    int                bytes;
    int                num;
    struct timeval        timeout;

    packet *prep = NULL;
    int my_name_length;
    packet *incoming_prep;
    char next_host_name[NAME_LENGTH] = {'\0'};
    char my_name[NAME_LENGTH] = {'\0'};

    printf("Start signal received!\n");
    gethostname(my_name, NAME_LENGTH);
    incoming_prep = calloc(1, PrepSizeWithNameLength(NAME_LENGTH));
    prep = calloc(1, PrepSizeWithNameLength(NAME_LENGTH));

    *ss = socket(AF_INET, SOCK_DGRAM, 0); /* Socket for sending */
    socket_s = *ss;
    if (socket_s<0) {
        perror("Mcast: socket");
        exit(1);
    }

    ttl_val = 1;
    if (setsockopt(socket_s, IPPROTO_IP, IP_MULTICAST_TTL, (void *)&ttl_val,
        sizeof(ttl_val)) < 0)
    {
        printf("Mcast: problem in setsockopt of multicast ttl %d - ignore in WinNT or Win95\n", ttl_val );
    }

    send_addr.sin_family = AF_INET;
    send_addr.sin_addr.s_addr = htonl(MCAST_ADDR);  /* mcast addresocket_s */
    send_addr.sin_port = htons(PORT);

    printf("sr = %d, ss = %d\n", sr, *ss);

    FD_ZERO( &mask );
    FD_ZERO( &dummy_mask );
    FD_SET( sr, &mask );
    for(;;)
    {
        timeout.tv_sec = 0;
        timeout.tv_usec = 1;
        temp_mask = mask;
        num = select( FD_SETSIZE, &temp_mask, &dummy_mask, &dummy_mask, &timeout);
        my_name_length = buildPrepPacket(&prep, machine_index, my_name);
        sendto( socket_s, prep, PrepSizeWithNameLength(my_name_length), 0, (struct sockaddr *)&send_addr, sizeof(send_addr) );
        if (num > 0) {
            if ( FD_ISSET( sr, &temp_mask) ) {
                bytes = recv( sr, incoming_prep, PrepSizeWithNameLength(NAME_LENGTH), 0 );
                printf("Incoming prep from machine %d received. \n", incoming_prep->sender_id);
                if (parsePrepPacket(incoming_prep, next_host_name, machine_index, number_of_machines)) {
                    printf("Next host name: %s\n", next_host_name);
                    break;
                } else {
                    /* It received the prep from other guys, ignore */
                    /* printf("NOT RIGHT ONE %d\n", incoming_prep->sender_id); */
                }
            }
        }
    }
}

void transmit(int number_of_packets, int machine_index, int number_of_machines, int loss_rate_percent, int sr, int ss) {
    fd_set             mask;
    fd_set             dummy_mask,temp_mask;
    int                bytes;
    int                num;
    struct timeval        timeout;
}

int nextMachineIndex(int machine_index, int number_of_machines) {
    if (machine_index + 1 < number_of_machines) {
        return machine_index + 1;
    } else {
        return 0;
    }
}

/**
 * Build a prep packet.
 * @return The length of host name.
 */
int buildPrepPacket(packet **prep, int machine_index, char *my_host_name) {
    int length;
    if (*prep) free(*prep);
    length = strlen(my_host_name);
    *prep = calloc(1, PrepSizeWithNameLength(length));
    (*prep)->sender_id = machine_index;
    (*prep)->type = PACKET_TYPE_PREP;
    strcpy((*prep)->contents, my_host_name);
    return length;
}

bool parsePrepPacket(packet *prep, char *next_host_name, int machine_index, int number_of_machines) {
    if (prep->type != PACKET_TYPE_PREP) {
        printf("Error: Type %d is not PREP packet.\n", prep->type);
        return false;
    }
    if (prep->sender_id == nextMachineIndex(machine_index, number_of_machines)) {
        /* If the sender of this prep packet is the next guy, process it */
        strcpy(next_host_name, prep->contents);
        return true;
    } else {
        return false;
    }
}

