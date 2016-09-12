/*
 * rcopy.c
 * By: Eric LaBouve
 * February Twenty-Fourth, Two-Thousand and Sixteen  
 *
 * All rights reserved
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

#include "cpe464.h"
#include "rcopy.h"
#include "networks.h"

//struct Connection server;

//uint32_t cur_RRnum;  //Dont think i need. Lowest RR - 1 == win_min
uint32_t cur_packet_num;
uint32_t win_min;
uint32_t win_cur;
uint32_t win_max;
uint32_t win_size;            //Get from commandline args
int32_t seq_num = START_SEQ_NUM;
uint8_t doneTransmitting = FALSE;
int32_t localFile;   //File Descriptor for the local file to read from
uint32_t bufSize;            //Get from commandline args
uint32_t timeout;        //IF == 10, Client should terminate
BufferChunk *window;



int main(int argc, char *argv[])
{
   Connection server;
   process_commandline_args(argc, argv);
   sendtoErr_init(atof(argv[4]), DROP_ON, FLIP_ON, DEBUG_OFF, RSEED_ON);
   udp_client_setup(argv[6], atoi(argv[7]), &server);

   timeout = 0;
   
   enter_state_machine(&server, (int8_t *)argv[2]); 

   //Client should return upon timeout or ACK_EOF from server
   return 0;
}

/*
 * State machine for client. Basically handles everything
 */
void enter_state_machine(Connection *server, int8_t *remoteFileName)
{
   STATE state = START;
   //int32_t packet_len = 0;
   //uint8_t fileDataBuf[bufSize];
   BufferChunk *sendPacket = (BufferChunk *) calloc(1, sizeof(BufferChunk));
   BufferChunk *recvPacket = (BufferChunk *) calloc(1, sizeof(BufferChunk));
   uint32_t SREJ_num = 0;  //Set when SREJ Packet received

   while (state != DONE)
   {
      switch (state)
      {
         case START:
            state = SEND_REMOTE_FILE_NAME;
            break;
         
         case WAIT_FOR_SETUP_ACKS:
            state = wait_for_setup_acks(server, recvPacket->buf, remoteFileName);
            break;
         
         case SEND_REMOTE_FILE_NAME:
            state = send_remote_file_name(server, remoteFileName, sendPacket->buf);
            break;
         
         case SEND_BUFFER_SIZE:
            state = send_buffer_size(server, bufSize, sendPacket->buf);
            break;
         
         case SEND_WINDOW_SIZE:
            state = send_window_size(server, win_size, sendPacket->buf);
            break;
         
        case READY_TO_SEND_DATA_SETUP:
            state = ready_to_send_data_setup(sendPacket->buf, recvPacket->buf);
            break;
         
          case SEND_DATA:
         //printf("---------------------win_cur = %d, min = %d, max = %d\n", win_cur, win_min, win_max);
            state = send_data(server, sendPacket->buf);
            break;

          case SEND_DATA_SREJ:
         //printf("---------------------win_cur = %d, min = %d, max = %d\n", win_cur, win_min, win_max);
            state = send_data_from_window(server, sendPacket->buf, SREJ_num - 1);
            break;

         case RECEIVE_DATA_WINDOW_OPEN:
            //printf("window open\n");
            state = receive_data_window_open(server, recvPacket->buf, &SREJ_num);
            break;

          case RECEIVE_DATA_WINDOW_CLOSED:
            state = receive_data_window_closed(server, recvPacket->buf, sendPacket->buf, &SREJ_num);
            break;
         
         case TIMEOUT_ON_SETUP_SERVER:
            state = timeout_on_setup_server(sendPacket->buf, recvPacket->buf);
            break;

          case TIMEOUT_ON_SERVER:
            state = timeout_on_server(sendPacket->buf, recvPacket->buf);
            break;

          case SEND_EOF_AND_QUIT:
            state = send_eof_and_quit(server, sendPacket->buf, recvPacket->buf);
            break;

         default:
            printf("RCOPY - You messed up!!!\n");
            break;

      }
   }
}

STATE receive_data_window_closed(Connection *server, uint8_t *recvPacket, uint8_t *sendPacket, uint32_t *SREJ_num)
{
   STATE state = RECEIVE_DATA_WINDOW_CLOSED;
   //Check if data is available 
   //printf("Recovery selecting\n");
   if (select_call(server->sk_num, 1, 0, NOT_NULL) == 1)
   {
      //printf("\n\n");
      timeout = 0;            //SREJ_num can also be an RR number
      state = process_incoming_data(server, recvPacket, SREJ_num);
   }
   //Data is not available
   else
   {
      timeout++;
      if (timeout > 10)
      {
         state = TIMEOUT_ON_SERVER;
      }
      if (timeout <= 10)
      {
         //Resend lowest packet in window;
         state = send_data_from_window(server, sendPacket, win_min);
      }
   }
   return state;

}

STATE receive_data_window_open(Connection *server, uint8_t *recvPacket, uint32_t *SREJ_num)
{          
   STATE state = SEND_DATA;
   //Check if data is available 
   //printf("selecting\n");
   if (select_call(server->sk_num, 0, 0, NOT_NULL) == 1)
   {
      timeout = 0;
      //printf("\n\nprocess incoming data...\n");
      state = process_incoming_data(server, recvPacket, SREJ_num);
   }
   /*Redundent but let's just check to be safe*/
   //Data is not available
   else
   {
      //IF window is open
      if (win_cur < win_max)
      {
         //printf("SEND DATA\n");
         state = SEND_DATA;  
      }
      //IF window is closed
      else
      {
         //printf("RECEIVE DATA WINDOW CLOSED\n"); 
         state = RECEIVE_DATA_WINDOW_CLOSED;  
      }
   }
   return state;
}

STATE process_incoming_data(Connection *server, uint8_t *recvPacket, uint32_t *SREJ_num)
{
   STATE state = SEND_DATA;
   uint32_t recv_len = 0;
   uint8_t flag = 0;
   uint32_t packet_num = 0;
   memset(recvPacket, 0, MAX_PACKET_LEN);

   if (isWindowClosed()) {
      state = RECEIVE_DATA_WINDOW_CLOSED;
   }
   //Read in next entire packet
   if ((recv_len = recv_buf(MAX_PACKET_LEN, server->sk_num, server, &flag, &seq_num, recvPacket)) != CRC_ERROR)
   {
      switch(flag)
      {
         case RR: 
         //Obtain RR number
         memcpy(&packet_num, &recvPacket[sizeof(PacketHeader)], sizeof(uint32_t));
         packet_num = ntohl(packet_num);
         win_min = packet_num - 1;     //win_min is an index
         win_max = packet_num + win_size - 1;   //win_max is an index
         //printf("RR %d RECEIVED\n", packet_num);
         
         //IF we are done sending and RRx = last RR
         if (doneTransmitting && (packet_num == cur_packet_num)) {
            state = SEND_EOF_AND_QUIT;
         }
         else {
            state = SEND_DATA;
         }
         break;
      
         case SREJ: 
         memcpy(&packet_num, &recvPacket[sizeof(PacketHeader)], sizeof(uint32_t));
         packet_num = ntohl(packet_num);
         //win_min = packet_num;
         //win_max = packet_num + win_size - 1;
         //printf("SREJ %d received\n", packet_num);
         *SREJ_num = packet_num;
         state = SEND_DATA_SREJ;
         break;
      
         case ACK_EOF:
         //printf("ACK_EOF received\n");
         state = DONE;
         break;

         default:      
         //printf("ERROR - UNIDENTIFIED PACKET RECEIVED. FLAG = %d\n", flag);  
         state = DONE;
         break;
      }  
   }
   return state; 
}

STATE send_data(Connection *server, uint8_t *sendPacket)
{
   STATE state = RECEIVE_DATA_WINDOW_OPEN;

   uint8_t fileData[MAX_PACKET_LEN];   //For the data file
   int32_t len_read = 0;               //For the data file len
   int32_t packet_len = 0;
   uint32_t window_slot = 0;
   //BufferChunk *chunk;
   uint8_t *temp = (uint8_t *) calloc(1, MAX_PACKET_LEN);

   memset(sendPacket, 0, MAX_PACKET_LEN);

   if ((len_read = read(localFile, fileData, bufSize)) < 0)
   {
      perror("send_data, read error\n");
      return DONE;
   }
   //zero indicates the end of the file
   if (len_read == 0)
   {
      //printf("End of file\n");
      doneTransmitting = TRUE;
      return RECEIVE_DATA_WINDOW_CLOSED; 
   }
   //Send data packet to server and save packet in window
   else
   {
      memcpy(temp, fileData, len_read);
     // printf("%s\n", (char *)temp);
      
      //Send packet
      //printf("------Sending packet number = %d\n", cur_packet_num);
      //printf("Sending sequence number = %d\n", seq_num);
      packet_len = send_buf_with_packet_num(fileData, len_read, server, DATA, seq_num, sendPacket, cur_packet_num);
      //Calculate Window Slot
      window_slot = win_cur % win_size;
      //printf("Window slot = %d\n", window_slot);
      seq_num++; win_cur++; cur_packet_num++;
      //Save packet data in window
      memcpy(&window[window_slot], sendPacket, MAX_PACKET_LEN);//packet_len);
      //Save packet length in window
      (&window[window_slot])->len = packet_len;
   }
   //Check if window is closed
   if (isWindowClosed()) {
      state = RECEIVE_DATA_WINDOW_CLOSED;
      //printf("window closed\n");
   }
   
   return state;
}

STATE send_data_from_window(Connection *server, uint8_t *sendPacket, uint32_t index)
{
   BufferChunk *chunk;  
   STATE state = RECEIVE_DATA_WINDOW_OPEN;
   //Retrieve location of old packet
   uint32_t window_slot = index % win_size;
   chunk = &window[window_slot];
   //Retrieve length of old packet
   uint32_t packet_len = chunk->len;
   //Copy old packet into our sendPacket
   memset(sendPacket, 0, MAX_PACKET_LEN);
   memcpy(sendPacket, &window[window_slot], MAX_PACKET_LEN);
   //Send old packet to server
   if (sendtoErr(server->sk_num, sendPacket, packet_len, 0, (struct sockaddr *)&(server->remote), server->len) < 0) {
      perror("send_data_from_window, sendToErr\n");
      exit(-1);
   }
   //Check if window is closed
   if (isWindowClosed()) {
      state = RECEIVE_DATA_WINDOW_CLOSED;
   }
   //printf("==>Resent packet #%d\n", (index + 1));
   return state;
}

//PRE: File, buffer, and window ACKs received from
STATE ready_to_send_data_setup(uint8_t *sendPacket, uint8_t *recvPacket)
{
   //Clear send and recv packets
   memset(sendPacket, 0, MAX_PACKET_LEN);
   memset(recvPacket, 0, MAX_PACKET_LEN);
   
   //Initializes |windowBuf| for packets [ | | | | ]
   window = (BufferChunk *) calloc(win_size, sizeof(BufferChunk));
   cur_packet_num = 1;
   win_min = 0;
   win_cur = 0;
   win_max = win_size;
   timeout = 0;

   return SEND_DATA;
}

STATE wait_for_setup_acks(Connection *server, uint8_t *recvPacket, int8_t *remoteFileName)
{
   static int8_t fileSuccess = FALSE;
   static int8_t bufSuccess = FALSE;
   static int8_t winSuccess = FALSE;
   int32_t recv_len = 0; 
   uint8_t flag = 0;
   int32_t seq_num = 0;
   STATE state = WAIT_FOR_SETUP_ACKS;
   //Select(1)
   //If data is available
   if (select_call(server->sk_num, 1, 0, NOT_NULL) == 1)
   {
      timeout = 0;
      //Clear |recvPacket|
      memset(recvPacket, 0, MAX_PACKET_LEN);
      //Fill in |recvPacket| w/incoming data if the data is good
      if ((recv_len = recv_buf(MAX_PACKET_LEN, server->sk_num, server, &flag, &seq_num, recvPacket)) != CRC_ERROR) {        
         if (flag  == ACK_FILE_NAME) {
            fileSuccess = TRUE;
            state = SEND_BUFFER_SIZE;
            //printf("SERVER ACKED THE FILE NAME\n");
         }
         else if (flag == REJ_FILE_NAME) { //Technically, wouldn't receive in this implementation
            state = SEND_REMOTE_FILE_NAME;
         }
         else if (flag == REJ_FILE_NAME_NO_OPEN) {
            printf("Error during file open of %s on Server\n", remoteFileName);
            state = DONE;
         }
         else if (flag  == ACK_BUFFER_SIZE) {
            bufSuccess = TRUE;
            state = SEND_WINDOW_SIZE;
            //printf("SERVER ACKED THE BUFFER SIZE\n");
         }
         else if (flag == REJ_BUFFER_SIZE) {
            state = SEND_BUFFER_SIZE;
            //printf("SERVER REJECTED THE BUFFER SIZE\n"); 
         }   
         else if (flag == ACK_WINDOW_SIZE) {
            winSuccess = TRUE;
            state = READY_TO_SEND_DATA_SETUP;
            //printf("SERVER ACKED THE WINDOW SIZE\n");
         }
         else if (flag == REJ_WINDOW_SIZE) {
            state = SEND_WINDOW_SIZE;
            //printf("SERVER REJECTED THE WINDOW SIZE\n");
         }
         else {
            //printf("Unidentified setup packet with flag = %d received\n", flag);
         
         }
      }
   }      
   //ELSE data is not available
   else
   {
      timeout++;          
      if (timeout > 10)
      {
         state = TIMEOUT_ON_SETUP_SERVER;
      }
      if (timeout <= 10)
      { 
         if (!fileSuccess)
            state = SEND_REMOTE_FILE_NAME;
         else if (!bufSuccess)
            state = SEND_BUFFER_SIZE;
         else
            state = SEND_WINDOW_SIZE;
      }
   }
   return state;
}


/* Send a FileName packet to the server
 * @param server: Our Connection to the server
 * @param remoteFileName: File name on the server
 */
STATE send_remote_file_name(Connection *server, int8_t *remoteFileName, uint8_t *sendPacket)
{
   //File name data
   uint8_t *dataBuf = (uint8_t *) calloc(1, MAX_PACKET_LEN);

   //Clear |sendPacket|
   memset(sendPacket, 0, sizeof(BufferChunk));
   //Fills in |sendPacket| with the remote file name
   //[4 Byte Len][File name]
   int32_t fileLen = strlen((const char *)remoteFileName);
   fileLen = htonl(fileLen);
   //printf("fileLen=%d\n", fileLen);
   memcpy(dataBuf, (int8_t *)&fileLen, sizeof(int32_t));
   fileLen = ntohl(fileLen);
   memcpy(dataBuf + sizeof(uint32_t), remoteFileName, fileLen);
  
   //Send remote file packet
   //printf("-->SENDING FILE NAME\n");
   send_buf(dataBuf, fileLen + sizeof(uint32_t), server, 
         FILE_NAME, seq_num++, sendPacket);
   return WAIT_FOR_SETUP_ACKS;
}


STATE send_buffer_size(Connection *server, uint32_t buf_size, uint8_t *sendPacket)
{
   //File name data
   uint8_t *dataBuf = (uint8_t *) calloc(1, MAX_PACKET_LEN);

   //Clear |sendPacket|
   memset(sendPacket, 0, sizeof(BufferChunk));
   
   //Fills in |sendPacket| with the buffer size [4 Byte Len]
   buf_size = htonl(buf_size);
   memcpy(dataBuf, (int8_t *)&buf_size, sizeof(int32_t));
   buf_size = ntohl(buf_size);

   //Send remote file packet
   //printf("-->SENDING BUFFER SIZE\n");
   send_buf(dataBuf, sizeof(uint32_t), server, 
         BUFFER_SIZE, seq_num++, sendPacket);

   return WAIT_FOR_SETUP_ACKS;
}


STATE send_window_size(Connection *server, uint32_t win_size, uint8_t *sendPacket)
{
   //File name data
   uint8_t *dataBuf = (uint8_t *) calloc(1, MAX_PACKET_LEN);

   //Clear |sendPacket|
   memset(sendPacket, 0, sizeof(BufferChunk));
   
   //Fills in |sendPacket| with the window size [4 Byte Len]
   win_size = htonl(win_size);
   memcpy(dataBuf, (int8_t *)&win_size, sizeof(int32_t));
   win_size = ntohl(win_size);

   //Send remote file packet
   //printf("-->SENDING WINDOW SIZE\n");
   send_buf(dataBuf, sizeof(uint32_t), server, 
         WINDOW_SIZE, seq_num++, sendPacket);

   return WAIT_FOR_SETUP_ACKS;
}

STATE send_eof_and_quit(Connection *server, uint8_t *sendPacket, uint8_t *recvPacket)
{
   STATE state = SEND_EOF_AND_QUIT;
   uint8_t dataBuf[1];
   uint32_t SREJ_num = 0;
   memset(sendPacket, 0, MAX_PACKET_LEN);
   memset(recvPacket, 0, MAX_PACKET_LEN);

   //Send EOF
   send_buf(dataBuf, 0, server, END_OF_FILE, seq_num++, sendPacket);  

   //Select(1)
   if (select_call(server->sk_num, 1, 0, NOT_NULL) == 1)
   {
      timeout = 0;
      //printf("process incoming data...\n");
      state = process_incoming_data(server, recvPacket, &SREJ_num);
   }
   //Data is not available
   else
   {
      timeout++;
      if (timeout > 10)
      {
         state = TIMEOUT_ON_SERVER;
      }
      if (timeout <= 10)
      {
         //Resend END_OF_FILE packet
         state = SEND_EOF_AND_QUIT;
      }
   }  
   return state;
}

uint8_t isWindowClosed()
{
   uint8_t answer = FALSE;
   if (doneTransmitting) {
      answer = TRUE;
      //printf("Window closed because we are done transmitting\n");
   }
   if (win_cur >= win_max) {
      answer = TRUE;
      //printf("Window closed because win_cur >= win_max\n");
   }
   return answer;
}

STATE timeout_on_setup_server(uint8_t *recvPacket, uint8_t *sendPacket)
{
   STATE state = DONE;
   free(recvPacket);
   free(sendPacket);
   perror("timeout_on_setup_server\n");
   return state;
}

STATE timeout_on_server(uint8_t *recvPacket, uint8_t *sendPacket)
{
   STATE state = DONE;
   free(recvPacket);
   free(sendPacket);
   free(window);
   perror("timeout_on_server\n");
   return state;
}
   
/*
 * Usage: rcopy local-file remote-file buffer-size error-percent window-size
 * remote-machine remote-port
 */
void process_commandline_args(int argc, char *argv[])
{
   //Client must run with all 8 parameters
   if (argc != 8)
   {
      printf("Usage: rcopy local-file remote-file buffer-size error-percent window-size remote-machine remote-port\n");
      exit(-1);
   }
   
   //Check if local file is okay size
   if (strlen(argv[1]) > 1000)
   {
      printf("Local file name needs to be less than 1000 characters");
      exit(-1);
   }
   
   //Check if remote file is okay size
   if (strlen(argv[2]) > 1000)
   {
      printf("Local file name needs to be less than 1000 characters");
      exit(-1);
   }
   
   //Open the file safely
//   if ((localFile = fopen(argv[1], "r")) < 0)
   if ((localFile = open(argv[1], O_RDONLY)) < 0)
   {
      printf("Local file named %s could not be opened\n", argv[1]);  
      exit(-1);
   }
   //printf("Using file : %s, with FD = %d\n", argv[1], localFile);

   //Check if buffer size is okay
   if (((bufSize = atoi(argv[3])) < 400) || (atoi(argv[3]) > 1400))
   {
      printf("Buffer size must be between 400 and 1400 bytes\n");
      exit(-1);
   }

   //Check if error percentage is okay
   if (atof(argv[4]) < 0 || atof(argv[4]) > 1)
   {
      printf("Error percentage must be between 0 and 1 inclusively\n");
      exit(-1);
   }

   //Check if window size is okay
   if ((win_size = atoi(argv[5])) < 0)
   {
      printf("Window size must be greater than 0\n");
      exit(-1);
   }
}

//SELECT AND RECEIVE DATA WITH WINDOW OPEN
          /* Select(0)
           * 
           * If data is available
           *    timeout = 0
           *    Clear |recvPacket|
           *    Fill in |recvPacket| w/incoming data
           *    
           *    If packet fails checksum
           *         set state to SEND_DATA(win_cur)
           *    
           *    If packet is good
           *        
           *        If packet is RRx where x = last data packet
           *            Set state to SEND_EOF_AND_QUIT
           *    
           *        If packet is RRx
           *            win_min = x, win_max = x + win_size - 1
           *            Set state to SEND_DATA(win_cur)
           *    
           *        If packet is SREJx && x is inside our window
           *            win_min = x, win_cur = x, win_max = x + win_size - 1
           *            Set state to SEND_DATA(x)
           *        ELSE you fucked up somewhere becuase the packet is 'good'
           * 
           * If data is NOT available
           *    
           *    If window is not closed //Not needed but let's check anyways
           *         Set state to SEND_DATA(win_cur)
           *    
           *    If window is closed
           *         Set state to RECEIVE_DATA_WINDOW_CLOSED
           */

//SEND DATA
          /* handle_send_data(location):
           * 
           * Clear |send_buf|
           *
           * If(location == win_cur)
           *   Read |bufSize| num bytes from data file into |fileDataBuf|
           *   Make and write packet with |fileDataBuf| into |sendBuf|
           *   Write |sendBuf| into windowBuf[win_cur % win_size]
           *   win_cur++
           * 
           * If(location != win_cur) //SREJx OR timeout
           *   Read packet at windowBuf[location % win_size] into |sendBuf|
           * 
           * Send |sendBuf|
           *
           * If win_cur >= win_max, window is closed, 
           *    set state to RECEIVE_DATA_WINDOW_CLOSED
           * If window not closed
           *    set state to RECEIVE_DATA_WINDOW_OPEN
           */
         

//SELECT AND RECEIVE DATA WITH WINDOW CLOSED
          /* Select(1)
           *
           * If data is available
           *    timeout = 0
           *    Clear |recvPacket|
           *    Fill in |recvPacket| w/incoming data
           *    
           *    If packet fails checksum
           *         set state to RECEIVE_DATA_WINDOW_CLOSED
           *    
           *    If packet is good
           *        
           *        If packet is RRx where x = last data packet
           *            Set state to SEND_EOF_AND_QUIT
           *    
           *        If packet is RRx
           *            win_min = x, win_cur = x, win_max = x + win_cur - 1
           *            Set state to SEND_DATA(win_cur)
           *    
           *        If packet is SREJx && x is inside our window
           *            win_min = x, win_cur = x, win_max = x + win_cur - 1
           *            Set state to SEND_DATA(x)
           *        ELSE you fucked up somewhere becuase the packet is 'good'
           * 
           * If data is NOT available
           *    timeout++
           *
           *    If timeout > 10
           *        Set state to TIMEOUT_ON_SERVER
           *
           *    If timeout <= 10
           *        Set state to SEND_DATA(win_min)
           */

