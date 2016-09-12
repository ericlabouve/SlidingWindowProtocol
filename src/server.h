#ifndef _SERVER_H_
#define _SERVER_H_

#include "networks.h"

typedef enum State STATE;

enum State
{
   START, 
   SETUP_CLIENT_SOCKET, 
   READY_FOR_NEXT_SETUP_PACKET, 
   RECEIVED_REMOTE_FILE_NAME, 
   RECEIVED_BUFFER_SIZE,
   RECEIVED_WINDOW_SIZE, 
   READY_FOR_DATA_PACKETS_SETUP, 
   READY_FOR_NEXT_DATA_PACKET,
   RECEIVED_DATA, 
   RECEIVED_EOF, 
   TIMEOUT_ON_CLIENT_SETUP, 
   TIMEOUT_ON_CLIENT,
   DONE
};


void process_commandline_args(int argc, char *argv[], uint16_t *portNum, float *errRate);
void process_client(int32_t server_sk_num, BufferChunk *recvPack, int32_t recv_len, Connection *client);

//STATE ready_for_next_setup_packet(Connection *client, uint8_t *recvPacket);
STATE ready_for_next_setup_packet(Connection *client, BufferChunk *recvPacket);
STATE received_remote_file_name(Connection *client, uint8_t *recvPacket, int32_t recvPacketLen, uint8_t *sendPacket);
STATE received_buffer_size(Connection *client, uint8_t *recvPacket, int32_t recvPacketLen, uint8_t *sendPacket);
STATE received_window_size(Connection *client, uint8_t *recvPacket, int32_t recvPacketLen, uint8_t *sendPacket);
STATE setup_client_socket(Connection *client);
STATE ready_for_data_packet_setup(uint8_t *recvPacket, uint8_t *sendPacket);
STATE ready_for_next_data_packet(Connection *client, BufferChunk *recvPacket);
STATE received_data(Connection *client, BufferChunk *recvPacket, BufferChunk *sendPacket);
STATE timeout_on_client_setup(BufferChunk *recvPacket, BufferChunk *sendPacket);
STATE timeout_on_client(BufferChunk *recvPacket, BufferChunk *sendPacket);
STATE received_eof(Connection *client, BufferChunk *recvPacket, BufferChunk *sendPacket);

void send_RR_or_SREJ(Connection *client, BufferChunk *sendPacket, uint32_t nextPacketNum);
uint8_t is_in_window(int32_t recvPackNum);
void buffer_packet(Connection *client, BufferChunk *recvPacket, BufferChunk *sendPacket, int32_t recvPackNum);
void handle_client_recovery(Connection *client, BufferChunk *recvPacket, BufferChunk *sendPacket);
void handle_expected_SREJ_packet(Connection *client, BufferChunk *recvPacket, BufferChunk *sendPacket);
void handle_expected_packet_number(Connection *client, BufferChunk *recvPacket, BufferChunk *sendPacket);
uint32_t check_for_and_write_buffered_packets(uint32_t cur_RR);
uint8_t are_packets_buffered();
void test_print_window();
#endif
