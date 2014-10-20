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
#include <time.h>

#include <errno.h>

#define PORT 10270

#define MCAST_ADDR 225 << 24 | 1 << 16 | 2 << 8 | 127

#define MAX_MESS_LEN 1400

#define WINDOW_SIZE 100

#define PACKET_TYPE_START 83
#define PACKET_TYPE_REGULAR 82
#define PACKET_TYPE_PREP 80
#define PACKET_TYPE_TOKEN 84

#define TOKEN_RTR_TIMEOUT (0.1 / 1000)

#define PrepSizeWithNameLength(name) sizeof(packet) + sizeof(bool) + sizeof(char) * (name + 1)
#define TokenSizeWithNackCount(count) sizeof(packet) + sizeof(int) * 5 + sizeof(int) * count
#define MessageSize sizeof(packet) + sizeof(int) * 2 + 1200

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

/* Token packet:
{
  char type;
  int sender_id;
  contents: {
    int msg_seq;
    int token_seq;
    int aru;
    int aru_id;
    int nack_count;
    int[] nacks;
  }
}
*/

/* Message packet:
{
  char type;
  int sender_id;
  contents: {
    int msg_seq;
    int random_number;
    char[] payload;
  }
}
*/
