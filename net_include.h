#include <stdio.h>

#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <netdb.h>

#include <errno.h>

#define PORT 10270

#define MCAST_ADDR 225 << 24 | 1 << 16 | 2 << 8 | 127

#define MAX_MESS_LEN 1400

#define PACKET_TYPE_START 1
#define PACKET_TYPE_REGULAR 2
#define PACKET_TYPE_PREP 0
#define PACKET_TYPE_TOKEN 3

#define PrepSizeWithNameLength(name) sizeof(packet) + sizeof(char) * (name + 1)

typedef struct
{
    char type;
    int sender_id;
    char contents[1];
} packet;

/* Prep packet:
{
  char type;
  int sender_id;
  contents: {
    char[] host_name;
  }
}

*/
