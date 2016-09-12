#ifndef _NETWORKS_H_
#define _NETWORKS_H_

#define TRUE 1
#define FALSE 0

#define START_SEQ_NUM 1
#define MAX_PACKET_LEN 1500

/*
#define FLAG_DATA 1  //C --> S
#define FLAG_RR 3    //S --> C
#define FLAG_SREJ 4  //S --> C

#define FLAG_FILE_NAME 6               //C --> S
#define FLAG_ACK_FILE_NAME 7           //S --> C
#define FLAG_REJ_FILE_NAME 8           //S --> C
#define FLAG_REJ_FILE_NAME_NO_OPEN 9   //S --> C

#define FLAG_BUFFER_SIZE 10         //C --> S
#define FLAG_ACK_BUFFER_SIZE 11     //S --> C
#define FLAG_REJ_BUFFER_SIZE 12     //S --> C

#define FLAG_WINDOW_SIZE 13         //C --> S
#define FLAG_ACK_WINDOW_SIZE 14     //S --> C
#define FLAG_REJ_WINDOW_SIZE 15     //S --> C

#define FLAG_EOF 16           //C --> S
#define FLAG_ACK_EOF 17       //S --> C
*/

enum FLAG
{
   INVALID_FLAG0,
   DATA, //C --> S
   INVALID_FLAG2,
   RR,   //S --> C
   SREJ, //S --> C
   INVALID_FLAG5,

   FILE_NAME,     //C --> S
   ACK_FILE_NAME, //S --> C
   REJ_FILE_NAME, //S --> C
   REJ_FILE_NAME_NO_OPEN,  //S --> C
   
   BUFFER_SIZE,      //C --> S
   ACK_BUFFER_SIZE,  //S --> C
   REJ_BUFFER_SIZE,  //S --> C

   WINDOW_SIZE,      //C --> S
   ACK_WINDOW_SIZE,  //S --> C
   REJ_WINDOW_SIZE,  //S --> C

   END_OF_FILE,      //C --> S
   ACK_EOF,          //S --> S
   CRC_ERROR = -1
};

enum SELECT {
   SET_NULL, NOT_NULL
};

typedef struct packetHeader PacketHeader;

struct packetHeader
{
   uint32_t sequenceNumber;   //In Network Order
   uint16_t checkSum; 
   uint8_t type;  //Indicate what type of data is in this packet
} __attribute__((packed));

typedef struct connection Connection;

struct connection
{
   int32_t sk_num;
   struct sockaddr_in remote;
   uint32_t len;
} __attribute__((packed));

typedef struct bufferChunk BufferChunk;

struct bufferChunk
{
   uint8_t buf[MAX_PACKET_LEN];
   uint32_t len;  //IF len == -1, The packet is not valid anymore
};

int32_t udp_server(uint16_t portNum);
int32_t udp_client_setup(char *hostname, uint16_t port_num, Connection *connection);
int32_t select_call(int32_t socket_num, int32_t seconds, int32_t microseconds, int32_t set_null);

int32_t recv_buf(/*uint8_t *buf,*/ int32_t len, int32_t recv_sk_num, Connection *connection, uint8_t *flag, int32_t *seq_num, uint8_t *recv_packet);

int32_t send_buf(uint8_t *buf, int32_t len, Connection *connection, uint8_t flag, uint32_t seq_num, uint8_t *packet);
int32_t send_buf_with_packet_num(uint8_t *buf, int32_t len, Connection *connection, uint8_t flag, uint32_t seq_num, uint8_t *packet, uint32_t packet_num);
//int32_t get_data_from_packet(uint8_t *recv_packet, int32_t packet_len, uint8_t *data);
int32_t get_data_from_packet(int32_t recv_len, uint8_t *recv_packet, uint8_t *recv_data);

#endif
