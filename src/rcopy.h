#ifndef _RCOPY_H_
#define _RCOPY_H_

#include "networks.h"

typedef enum State STATE;

enum State
{
   START, 
   SEND_REMOTE_FILE_NAME, 
   WAIT_FOR_SETUP_ACKS, 
   SEND_BUFFER_SIZE, 
   SEND_WINDOW_SIZE, 
   READY_TO_SEND_DATA_SETUP, 
   SEND_DATA, 
   SEND_DATA_SREJ,
   RECEIVE_DATA_WINDOW_OPEN, 
   RECEIVE_DATA_WINDOW_CLOSED, 
   TIMEOUT_ON_SETUP_SERVER, 
   TIMEOUT_ON_SERVER,
   SEND_EOF_AND_QUIT, 
   DONE
};

void process_commandline_args(int argc, char *argv[]);
void enter_state_machine(Connection *server, int8_t *remoteFileName);

STATE wait_for_setup_acks(Connection *server, uint8_t *recvPacket, int8_t *remoteFileName);
STATE send_remote_file_name(Connection *server, int8_t *remoteFileName, uint8_t *sendPacket);
STATE send_buffer_size(Connection *server, uint32_t buf_size, uint8_t *sendPacket);
STATE send_window_size(Connection *server, uint32_t win_size, uint8_t *sendPacket);
STATE ready_to_send_data_setup(uint8_t *sendPacket, uint8_t *recvPacket);
STATE send_data(Connection *server, uint8_t *sendPacket);
STATE send_data_from_window(Connection *server, uint8_t *sendPacket, uint32_t index);
STATE receive_data_window_open(Connection *server, uint8_t *recvPacket, uint32_t *SREJ_num);
STATE receive_data_window_closed(Connection *server, uint8_t *recvPacket, uint8_t *sendPacket, uint32_t *SREJ_num);
STATE timeout_on_setup_server(uint8_t *recvPacket, uint8_t *sendPacket);
STATE timeout_on_server(uint8_t *recvPacket, uint8_t *sendPacket);
STATE send_eof_and_quit(Connection *server, uint8_t *sendPacket, uint8_t *recvPacket);
STATE process_incoming_data(Connection *server, uint8_t *recvPacket, uint32_t *SREJ_num);

uint8_t isWindowClosed();
#endif
