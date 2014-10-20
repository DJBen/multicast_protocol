#include "net_include.h"

#define NAME_LENGTH 80

int gethostname(char*,size_t);
void waitForStart(int number_of_packets, int machine_index, int number_of_machines, int loss_rate_percent, int *sr);
void prepare(int number_of_packets, int machine_index, int number_of_machines, int loss_rate_percent, int sr, int *ss, char **next_host_name, packet **first_packet);
void transmit(int number_of_packets, int machine_index, int number_of_machines, int loss_rate_percent, int sr, int ss, char *next_host_name, packet *first_packet);

int nextMachineIndex(int machine_index, int number_of_machines);
int buildPrepPacket(packet **prep, int machine_index, char *my_host_name, bool received_next_host_name);
void buildTokenPacket(packet **token, int machine_index, int msg_seq, int token_seq, int aru, int aru_id, int nack_count, int *nacks);
void parseTokenPacket(packet *token, int *machine_index, int *msg_seq, int *token_seq, int *aru, int *aru_id, int *nack_count, int *nacks);
int buildMessagePacket(packet **message, int machine_index, int msg_seq);
void parseMessagePacket(packet *message, int *machine_index, int *msg_seq, int *random_number);
void dump_nacks(int *nacks, int nack_count);

int main(int argc, char const *argv[])
{
    int loss_rate_percent;
    int machine_index;
    int number_of_machines;
    int number_of_packets;

    int sr = -1, ss = -1;
    char *next_host_name = NULL;
    packet *first_packet = NULL;

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
    prepare(number_of_packets, machine_index, number_of_machines, loss_rate_percent, sr, &ss, &next_host_name, &first_packet);
    transmit(number_of_packets, machine_index, number_of_machines, loss_rate_percent, sr, ss, next_host_name, first_packet);

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

    struct sockaddr_in    from_addr;
    socklen_t             from_len;

    start_sig = malloc(sizeof(packet));
    socket_r = socket(AF_INET, SOCK_DGRAM, 0); /* socket for receiving */
    if (socket_r <0) {
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
    *sr = socket_r;

    FD_ZERO( &mask );
    FD_ZERO( &dummy_mask );
    FD_SET( socket_r, &mask );
    for(;;)
    {
        temp_mask = mask;
        num = select( FD_SETSIZE, &temp_mask, &dummy_mask, &dummy_mask, NULL);
        if (num > 0) {
            if ( FD_ISSET( socket_r, &temp_mask) ) {
                from_len = sizeof(from_addr);
                bytes = recvfrom( socket_r, start_sig, sizeof(packet), 0, (struct sockaddr *)&from_addr, &from_len );
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

void prepare(int number_of_packets, int machine_index, int number_of_machines, int loss_rate_percent, int sr, int *ss, char **out_next_host_name, packet **first_packet) {

    struct sockaddr_in send_addr;

    unsigned char      ttl_val;
    u_char loop;

    int                socket_s;
    fd_set             mask;
    fd_set             dummy_mask,temp_mask;
    int                bytes;
    int                num;
    struct timeval        timeout;

    struct sockaddr_in    from_addr;
    socklen_t             from_len;

    int i;
    packet *prep = NULL;
    int my_name_length;
    packet *incoming_prep;
    char next_host_name[NAME_LENGTH] = {'\0'};
    char my_name[NAME_LENGTH] = {'\0'};
    bool received_next_host_name = false;
    bool *recvs = NULL;
    bool is_from_next_host = false;
    bool is_everyone_received = false;

    printf("Start signal received!\n");
    gethostname(my_name, NAME_LENGTH);
    incoming_prep = calloc(1, PrepSizeWithNameLength(NAME_LENGTH));

    /* Only machine 0 has to care about the recv vectors */
    if (machine_index == 0) {
        recvs = calloc(number_of_machines, sizeof(bool));
    }

    socket_s = socket(AF_INET, SOCK_DGRAM, 0); /* Socket for sending */
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

    loop = 0;
    if (setsockopt(socket_s, IPPROTO_IP, IP_MULTICAST_LOOP, &loop, sizeof(loop)) < 0)
    {
      printf("Mcast: problem in setsockopt of multicast loop\n");
    }

    *ss = socket_s;

    send_addr.sin_family = AF_INET;
    send_addr.sin_addr.s_addr = htonl(MCAST_ADDR);  /* mcast addresocket_s */
    send_addr.sin_port = htons(PORT);

    FD_ZERO( &mask );
    FD_ZERO( &dummy_mask );
    FD_SET( sr, &mask );
    for(;;)
    {
        timeout.tv_sec = 0;
        timeout.tv_usec = 5;
        temp_mask = mask;
        num = select( FD_SETSIZE, &temp_mask, &dummy_mask, &dummy_mask, &timeout);
        my_name_length = buildPrepPacket(&prep, machine_index, my_name, received_next_host_name);
        sendto( socket_s, prep, PrepSizeWithNameLength(my_name_length), 0, (struct sockaddr *)&send_addr, sizeof(send_addr));
        if (num > 0) {
            if ( FD_ISSET( sr, &temp_mask) ) {
                from_len = sizeof(from_addr);
                bytes = recvfrom( sr, incoming_prep, PrepSizeWithNameLength(NAME_LENGTH), 0, (struct sockaddr *)&from_addr, &from_len );

                printf(">> Incoming prep from machine %d received. \n", incoming_prep->sender_id);

                /* Parse the prep packet */
                if (incoming_prep->type != PACKET_TYPE_PREP) {
                    /* If received a token or regular message, go to transmission stage */
                    if (incoming_prep->type == PACKET_TYPE_TOKEN || incoming_prep->type == PACKET_TYPE_REGULAR) {
                        *first_packet = malloc(bytes);
                        memcpy(*first_packet, incoming_prep, bytes);
                        break;
                    } else {
                        /* Normal machines: Go to transmission stage */
                        printf("Error: Type %d is not PREP packet.\n", incoming_prep->type);
                    }
                }

                if (machine_index == 0) {
                    memcpy(recvs + incoming_prep->sender_id, incoming_prep->contents, sizeof(bool));
                }
                is_from_next_host = incoming_prep->sender_id == nextMachineIndex(machine_index, number_of_machines);
                if (is_from_next_host) {
                    /* If the sender of this prep packet is the next guy, process it */
                    strcpy(next_host_name, incoming_prep->contents + sizeof(bool));
                    received_next_host_name = true;
                    if (machine_index == 0) recvs[0] = true;
                    printf("Next host name: %s\n", next_host_name);
                    *out_next_host_name = malloc((strlen(next_host_name) + 1) * sizeof(char));
                    strcpy(*out_next_host_name, next_host_name);
                } else {
                    /* It received the prep from other guys, ignore */
                }

                /* Machine 0: check for start condition: all others have received their next machines' name. */
                if (machine_index == 0) {
                    is_everyone_received = true;
                    for (i = 0; i < number_of_machines; ++i)
                    {
                        if (recvs[i] == 0) is_everyone_received = false;
                    }
                    if (is_everyone_received) {
                        /* Go to transmission stage */
                        printf("Machine 0: preparing to go to TRANSMISSION stage\n");
                        free(recvs);
                        break;
                    }
                }

            }
        }
    }
}

void transmit(int number_of_packets, int machine_index, int number_of_machines, int loss_rate_percent, int sr, int ss, char *next_host_name, packet *first_packet) {
    fd_set             mask;
    fd_set             dummy_mask,temp_mask;
    int                bytes;
    int                num;
    struct timeval        timeout;
    struct hostent        h_ent;
    struct hostent        *p_h_ent;
    struct sockaddr_in    send_addr;
    struct sockaddr_in    token_send_addr;
    int                   host_num;

    int my_aru = -1;
    int my_token_seq;
    int temp_msg_seq;
    int last_aru = -1; /* aru used to calculate safe messages */
    int prev_round_aru = -1;
    int my_nack_count = 0;
    int *my_nacks = NULL;
    int missing_message_seq;

    int token_seq = 0;
    int msg_seq = -1;
    int aru = -1;
    int prev_aru_before_update = -1;
    bool newly_update_aru = false;
    int aru_id = -1;
    int nack_count = 0;
    int nacks[WINDOW_SIZE * number_of_machines];
    int i;

    packet **message_queue;
    packet *temp_message = NULL;
    packet *temp_token = NULL;
    packet *received_packet;
    int random_number = 0;
    int sender_machine_index = 0;

    int token_ss;
    bool token_retransmission_on = false;
    clock_t rtr_clock, now_clock;

    struct sockaddr_in    from_addr;
    socklen_t             from_len;

    FILE *fw = NULL;
    char file_name[NAME_LENGTH];

    printf("TRANSMISSION stage.\n");

    my_token_seq = machine_index - 2;
    sprintf(file_name, "%d.out", machine_index);
    if ((fw = fopen(file_name, "w")) == NULL) {
        perror("fopen");
        exit(0);
    }

    token_ss = socket(AF_INET, SOCK_DGRAM, 0); /* Socket for sending tokens */
    if (token_ss<0) {
        perror("Mcast: socket");
        exit(1);
    }

    received_packet = calloc(1, MessageSize); /* Generally, the Message packet has the largest size */

    message_queue = calloc(WINDOW_SIZE * number_of_machines, sizeof(packet *));
    printf("next host: %s\n", next_host_name);

    my_nacks = calloc(WINDOW_SIZE * number_of_machines, sizeof(int));

    /* Initialize token_send_addr, the address of next host. */
    p_h_ent = gethostbyname(next_host_name);
    if ( p_h_ent == NULL ) {
        printf("Ucast: gethostbyname error.\n");
        exit(1);
    }

    memcpy( &h_ent, p_h_ent, sizeof(h_ent));
    memcpy( &host_num, h_ent.h_addr_list[0], sizeof(host_num) );

    token_send_addr.sin_family = AF_INET;
    token_send_addr.sin_addr.s_addr = host_num;
    token_send_addr.sin_port = htons(PORT);

    send_addr.sin_family = AF_INET;
    send_addr.sin_addr.s_addr = htonl(MCAST_ADDR);  /* mcast addresocket_s */
    send_addr.sin_port = htons(PORT);

    /* Initiate as machine 0 */
    if (machine_index == 0) {
        buildTokenPacket(&first_packet, machine_index, msg_seq, token_seq, aru, aru_id, my_nack_count, my_nacks);
    }

    FD_ZERO( &mask );
    FD_ZERO( &dummy_mask );
    FD_SET( sr, &mask );
    for (;;) {
        if (first_packet != NULL) {
            received_packet = first_packet;
        } else {
            timeout.tv_sec = 0;
            timeout.tv_usec = 5;
            temp_mask = mask;
            /* printf("-->SELECT BEFORE\n"); */
            num = select( FD_SETSIZE, &temp_mask, &dummy_mask, &dummy_mask, &timeout);
            if (num > 0 && FD_ISSET( sr, &temp_mask) ) {
                /* printf("-->SELECT AFTER\n"); */
                from_len = sizeof(from_addr);
                bytes = recvfrom( sr, received_packet, MessageSize, 0, (struct sockaddr *)&from_addr, &from_len );
            }
        }
        if ((num > 0 && FD_ISSET( sr, &temp_mask)) || first_packet != NULL) {
            first_packet = NULL;
            if (received_packet->type == PACKET_TYPE_REGULAR) {
                token_retransmission_on = false;
                /* printf("-->BEFORE REGULAR\n"); */
                parseMessagePacket(received_packet, &sender_machine_index, &msg_seq, &random_number);
                /* printf("-->AFTER REGULAR\n"); */
                if (msg_seq > my_aru) {
                    /* Save to buffer*/
                    /* if (message_queue[msg_seq % (WINDOW_SIZE * number_of_machines)]) {
                        free(message_queue[msg_seq % (WINDOW_SIZE * number_of_machines)]);
                    } */
                    message_queue[msg_seq % (WINDOW_SIZE * number_of_machines)] = received_packet;
                    printf("Received message #%d: %d", msg_seq, random_number);

                    /* Update aru */
                    for (i = my_aru + 1; i <= my_aru + (WINDOW_SIZE * number_of_machines); i++) {
                        if (message_queue[i % (WINDOW_SIZE * number_of_machines)] == NULL) {
                            break;
                        }
                        parseMessagePacket(message_queue[i % (WINDOW_SIZE * number_of_machines)], &sender_machine_index, &msg_seq, &random_number);
                        printf(", Write #%d: %d from %d", msg_seq, random_number, sender_machine_index);
                        /*fprintf(fw, "%2d, %8d, %8d\n", sender_machine_index, msg_seq, random_number);*/
                    }
                    my_aru = i - 1;
                    printf(", Update aru = #%d\n", my_aru);
                } else {
                    /* Message too old */
                    printf("Discard old #%d: %d\n", msg_seq, random_number);
                }
            } else if (received_packet->type == PACKET_TYPE_TOKEN) {
                /* printf("-->BEFORE TOKEN PARSE\n"); */
                parseTokenPacket(received_packet, &sender_machine_index, &msg_seq, &token_seq, &aru, &aru_id, &nack_count, nacks);
                /* printf("-->AFTER TOKEN PARSE\n"); */
                printf("Received token #%d: from %d, msg_seq %d, aru %d, aru_id %d, nack_count %d\n", token_seq, sender_machine_index, msg_seq, aru, aru_id, nack_count);
                if (token_seq > my_token_seq) {
                    token_retransmission_on = false;
                    printf("Received valid token: #%d!\n", token_seq);
                    newly_update_aru = false;
                    /* Compare my_aru with aru. If aru is greater, set it to my_aru and set the id. */
                    if (my_aru < aru) {
                        aru = my_aru;
                        aru_id = machine_index;
                    } else if (aru_id == machine_index) {
                        prev_aru_before_update = aru;
                        newly_update_aru = true;
                        aru = my_aru;
                    }
                    if (aru == msg_seq) {
                        aru_id = -1;
                    }

                    printf("last aru = %d, this aru = %d\n", last_aru, aru);
                    /* Delete the already safe packet from the queue */
                    for (i = last_aru + 1; i <= aru; i++) {
                        if (message_queue[i % (WINDOW_SIZE * number_of_machines)] != NULL) {
                            /*free(message_queue[i % (WINDOW_SIZE * number_of_machines)]);*/
                            message_queue[i % (WINDOW_SIZE * number_of_machines)] = NULL;
                            printf("Message %d safe.\n", i);
                        }
                    }
                    last_aru = aru;

                    /* Send messages, nacks first */
                    printf("Retransmission begins\n");
                    for (i = 0; i < nack_count; i++) {
                        missing_message_seq = nacks[i];
                        /* I don't have that packet either */
                        if (message_queue[missing_message_seq % (WINDOW_SIZE * number_of_machines)] == NULL) {
                            printf("Doesn't have %d (%d in buffer) either\n", missing_message_seq, missing_message_seq % (WINDOW_SIZE * number_of_machines));
                            continue;
                        }
                        /* Send nacks */
                        sendto(ss, message_queue[missing_message_seq % (WINDOW_SIZE * number_of_machines)], MessageSize, 0, (struct sockaddr *)&send_addr, sizeof(send_addr));
                        printf("Retransmitting %d (%d in buffer).\n", missing_message_seq, missing_message_seq % (WINDOW_SIZE * number_of_machines));
                    }

                    /* Send my own new packets */
                    temp_msg_seq = msg_seq;
                    for (i = msg_seq + 1; i <= (newly_update_aru ? prev_aru_before_update : aru) + WINDOW_SIZE && number_of_packets > 0; i++) {
                        if (i - my_aru == 1) my_aru = i;
                        temp_msg_seq = i;
                        number_of_packets--;
                        random_number = buildMessagePacket(&temp_message, machine_index, i);
                        message_queue[i % (WINDOW_SIZE * number_of_machines)] = temp_message;
                        bytes = sendto(ss, temp_message, MessageSize, 0, (struct sockaddr *)&send_addr, sizeof(send_addr));
                        printf("Machine %d: send #%d: %d - %d\n", machine_index, i, random_number, bytes);
                    }
                    if (aru == msg_seq) {
                        aru = temp_msg_seq;
                        my_aru = temp_msg_seq;
                    }
                    msg_seq = temp_msg_seq;

                    /* Construct my own nacks */
                    my_nack_count = 0;
                    for (i = my_aru + 1; i <= msg_seq; i++) {
                        if (message_queue[i % (WINDOW_SIZE * number_of_machines)] == NULL) {
                            my_nacks[my_nack_count++] = i;
                        }
                    }

                    /* Send token to the next machine */
                    buildTokenPacket(&temp_token, machine_index, msg_seq, token_seq + 1, aru, aru_id, my_nack_count, my_nacks);
                    sendto(ss, temp_token, TokenSizeWithNackCount(my_nack_count), 0, (struct sockaddr *)&token_send_addr, sizeof(token_send_addr));
                    token_retransmission_on = true;
                    rtr_clock = clock();
                    printf("Sent token #%d (msg_seq %d, aru %d, aru_id %d, nack_count %d) to next machine.\n", token_seq + 1, msg_seq, aru, aru_id, my_nack_count);

                    /* Increase token seq number*/
                    my_token_seq += number_of_machines;

                    /* If msg_seq and aru are the same, nacks is empty in consecutive rounds, finish transmission. */
                    /* Retransmit the token */
                    if (nack_count == 0 && my_nack_count == 0 && aru == msg_seq && prev_round_aru == aru) {
                        printf("Finish transmission with aru = %d\n", aru);
                        break;
                    }
                    prev_round_aru = aru;
                } else {
                   /* Redundant token, ignore */
                    printf("Received redundant token: #%d!\n", token_seq);
                }
            } else if (received_packet->type == PACKET_TYPE_PREP) {
                /* Ignore */
                /* printf("-->PREP\n"); */
            } else {
                printf("Error: transmission stage should not receive packet type %d\n", received_packet->type);
            }
        }
        if (token_retransmission_on) {
            /* Token retransmission */
            now_clock = clock() - rtr_clock;
            /* printf("%f\n", (now_clock) / (double)CLOCKS_PER_SEC); */
            if ((now_clock) / (double)CLOCKS_PER_SEC > TOKEN_RTR_TIMEOUT) {
                /* printf("> %f\n", (now_clock) / (double)CLOCKS_PER_SEC); */
                sendto(ss, temp_token, TokenSizeWithNackCount(my_nack_count), 0, (struct sockaddr *)&token_send_addr, sizeof(token_send_addr));
                rtr_clock = clock();
                printf("Retransmit token #%d (msg_seq %d, aru %d, nack_count %d) to next machine.\n", token_seq + 1, msg_seq, aru, my_nack_count);
            }
        }
    }
    fclose(fw);
    fw = NULL;
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
int buildPrepPacket(packet **prep, int machine_index, char *my_host_name, bool received_next_host_name) {
    int length;
    if (*prep) free(*prep);
    length = strlen(my_host_name);
    *prep = calloc(1, PrepSizeWithNameLength(length));
    (*prep)->sender_id = machine_index;
    (*prep)->type = PACKET_TYPE_PREP;
    memcpy((*prep)->contents, &received_next_host_name, sizeof(bool));
    strcpy((*prep)->contents + sizeof(bool), my_host_name);
    return length;
}

void buildTokenPacket(packet **token, int machine_index, int msg_seq, int token_seq, int aru, int aru_id, int nack_count, int *nacks) {
    if (*token) free(*token);
    *token = calloc(1, TokenSizeWithNackCount(nack_count));
    (*token)->sender_id = machine_index;
    (*token)->type = PACKET_TYPE_TOKEN;
    memcpy((*token)->contents, &msg_seq, sizeof(int));
    memcpy((*token)->contents + sizeof(int), &token_seq, sizeof(int));
    memcpy((*token)->contents + sizeof(int) * 2, &aru, sizeof(int));
    memcpy((*token)->contents + sizeof(int) * 3, &aru_id, sizeof(int));
    memcpy((*token)->contents + sizeof(int) * 4, &nack_count, sizeof(int));
    memcpy((*token)->contents + sizeof(int) * 5, nacks, sizeof(int) * nack_count);
    printf("My ");
    dump_nacks(nacks, nack_count);
}

void parseTokenPacket(packet *token, int *machine_index, int *msg_seq, int *token_seq, int *aru, int *aru_id, int *nack_count, int *nacks) {
    if (token->type != PACKET_TYPE_TOKEN) {
        printf("Error: Packet type %d is not token!\n", token->type);
        return;
    }
    *machine_index = token->sender_id;
    memcpy(msg_seq, token->contents, sizeof(int));
    memcpy(token_seq, token->contents + sizeof(int), sizeof(int));
    memcpy(aru, token->contents + 2 * sizeof(int), sizeof(int));
    memcpy(aru_id, token->contents + 3 * sizeof(int), sizeof(int));
    memcpy(nack_count, token->contents + 4 * sizeof(int), sizeof(int));
    memcpy(nacks, token->contents + 5 * sizeof(int), sizeof(int) * *nack_count);
    printf("Machine %d 's ", *machine_index);
    dump_nacks(nacks, *nack_count);
}

/* Unlike other build methods, this one doesn't free the pointer of message. */
int buildMessagePacket(packet **message, int machine_index, int msg_seq) {
    int random_number;
    *message = calloc(1, MessageSize);
    random_number = rand() % 1000000 + 1;
    (*message)->sender_id = machine_index;
    (*message)->type = PACKET_TYPE_REGULAR;
    memcpy((*message)->contents, &msg_seq, sizeof(int));
    memcpy((*message)->contents + sizeof(int), &random_number, sizeof(int));
    memset((*message)->contents + sizeof(int) * 2, 0, 1200);
    return random_number;
}

void parseMessagePacket(packet *message, int *machine_index, int *msg_seq, int *random_number) {
    if (message->type != PACKET_TYPE_REGULAR) {
        printf("Error: Packet type %d is not message!\n", message->type);
        return;
    }
    *machine_index = message->sender_id;
    memcpy(msg_seq, message->contents, sizeof(int));
    memcpy(random_number, message->contents + sizeof(int), sizeof(int));
}

void dump_nacks(int *nacks, int nack_count) {
    int i;
    printf("Nacks: ");
    for (i = 0; i < nack_count; i++) {
        printf("%d ", nacks[i]);
    }
    printf("\n");
}

