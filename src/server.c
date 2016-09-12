
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
#include "server.h"
#include "networks.h"

uint32_t cur_RRnum;
uint32_t win_min;
uint32_t win_cur;
uint32_t win_max;
uint32_t win_size;            //Get from Window Packet
uint8_t has_SREJ = FALSE;
uint32_t num_SREJ;

int32_t seq_num = START_SEQ_NUM;
int8_t *fileName;              //Get from File Packet
int32_t fileFD;
uint32_t timeout;
int8_t fileGood;
int8_t bufGood;
int8_t winGood;
uint32_t bufSize;            //Get from Buffer Packet
BufferChunk *window;

int main(int argc, char *argv[])
{
   int32_t server_sk_num = 0;
   uint16_t portNum = 0;
   float errRate;
   pid_t pid = 0;
   int32_t status = 0;   //Used in waitpid()
   fileFD = -1; 
   fileGood = FALSE;
   bufGood = FALSE;
   winGood = FALSE;

   //Buffer for receiving packets
   BufferChunk *recvPacket = (BufferChunk *) calloc(1, sizeof(BufferChunk));

   Connection client;
   uint8_t flag = 0;    //The type of the packet received!! Extract this shit from the packet
   int32_t seq_num = 0;
   int32_t recv_len = 0;
   
   
   process_commandline_args(argc, argv, &portNum, &errRate);  

   sendErr_init(errRate, DROP_ON, FLIP_ON, DEBUG_OFF, RSEED_ON);  

   /*Set up the main server port*/
   server_sk_num = udp_server(portNum);

   //Loop forever
   while (1) 
   {
      if (select_call(server_sk_num, 1, 0, NOT_NULL) == 1)
      {         
         recv_len = recv_buf(MAX_PACKET_LEN, server_sk_num, &client, &flag, &seq_num, recvPacket->buf);
         
         //dont fork if recv_len == CRC Failure
         if (recv_len != CRC_ERROR)
         {
            if ((pid = fork()) < 0)
            {
               perror("fork");
               exit(-1);
            }
            //process child
            if (pid == 0)
            {
               //printf("LOL I actually found someone\n");
               process_client(server_sk_num, recvPacket, recv_len, &client);
               exit(0);
            }
         }

         while (waitpid(-1, &status, WNOHANG) > 0)
         {
            //printf("processed wait\n");
         }
      }
   }
   
   //Should never return
   return 0;
}

void process_client(int32_t server_sk_num, BufferChunk *recvPack, int32_t recv_len, Connection *client)
{
   STATE state = START;
   int32_t recvPacketLen = recv_len;
   //uint8_t fileDataBuf[bufSize]; bufSize is also undeclared at this point
   BufferChunk *sendPacket = (BufferChunk *) calloc(1, sizeof(BufferChunk));
   BufferChunk *recvPacket = (BufferChunk *) calloc(1, sizeof(BufferChunk));


   //Copy first packet received into |recvPacket|
   //memcpy(recvPacket->buf, buff, recv_len);// + sizeof(PacketHeader) + 1);
   memcpy(recvPacket->buf, recvPack->buf, recv_len);// + sizeof(PacketHeader) + 1);

   while (state != DONE)
   {
      switch (state)
      {
        case START:
            state = SETUP_CLIENT_SOCKET;//RECEIVED_REMOTE_FILE_NAME;
            break;

        case SETUP_CLIENT_SOCKET:
            state = setup_client_socket(client);
            break;
         
        case READY_FOR_NEXT_SETUP_PACKET:
            state = ready_for_next_setup_packet(client, recvPacket);//recvPacket->buf);
            break;

        case RECEIVED_REMOTE_FILE_NAME:
            state = received_remote_file_name(client, recvPacket->buf, recvPacketLen, sendPacket->buf);
            break;

         case RECEIVED_BUFFER_SIZE:
            state = received_buffer_size(client, recvPacket->buf, recvPacketLen, sendPacket->buf);
            break;
          
         case RECEIVED_WINDOW_SIZE:
            state = received_window_size(client, recvPacket->buf, recvPacketLen, sendPacket->buf);
            break;

         case READY_FOR_DATA_PACKETS_SETUP:
            state = ready_for_data_packet_setup(recvPacket->buf, sendPacket->buf);
            break;
          
         case READY_FOR_NEXT_DATA_PACKET:
            state = ready_for_next_data_packet(client, recvPacket);
            break;

         case RECEIVED_DATA:
            state = received_data(client, recvPacket, sendPacket);
            break;

         case RECEIVED_EOF:
            state = received_eof(client, recvPacket, sendPacket);
            break;

         case TIMEOUT_ON_CLIENT_SETUP:
            state = timeout_on_client_setup(recvPacket, sendPacket);
            break;

         case TIMEOUT_ON_CLIENT:
            state = timeout_on_client(recvPacket, sendPacket);

         case DONE:
            break;

         default:
            //printf("SERVER - You messed up!!!\n");
            state = DONE;
            break;
      }
   }
}

/* Opens up a socket for talking to the client
 * @param client: Connection to one client
 */
STATE setup_client_socket(Connection *client)
{
   //Open up a socket for the client
   if ((client->sk_num = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
   {
      perror("setup_client_socket, open client socket\n");
      exit(-1);
   }
   //printf("changed state to RECEIVED REMOTE FILE NAME\n");

   return RECEIVED_REMOTE_FILE_NAME;
}

STATE ready_for_next_data_packet(Connection *client, BufferChunk *recvPacket)
{
   STATE state = READY_FOR_NEXT_DATA_PACKET;
   uint32_t recv_len = 0;
   uint8_t flag = 0;
   //uint32_t packet_num = 0;
   memset(recvPacket, 0, MAX_PACKET_LEN);

   //Check if data is available
   if (select_call(client->sk_num, 10, 0, NOT_NULL) == 1)
   {
      //printf("\n\n\nPacket found. >");
      timeout = 0;
      
      //Read in next entire packet
      if ((recv_len = recv_buf(MAX_PACKET_LEN, client->sk_num, client, &flag, &seq_num, recvPacket->buf)) != CRC_ERROR)
      {
         switch(flag)
         {
            case DATA:
               //Set the length of the packet received
               recvPacket->len = recv_len;
               state = RECEIVED_DATA;
            break;

            case END_OF_FILE:
               state = RECEIVED_EOF;
            break;
         }
      }
   }
   else
   {
      state = TIMEOUT_ON_CLIENT;
   }
   return state;
}

STATE received_data(Connection *client, BufferChunk *recvPacket, BufferChunk *sendPacket)
{
   STATE state = READY_FOR_NEXT_DATA_PACKET;
   int32_t recvPackNum = -1;
   //uint32_t nextPacketNum = -1;      //The next expected packet num after cleaning window buffer
   //uint8_t fileData[MAX_PACKET_LEN];
   //memset(fileData, 0, MAX_PACKET_LEN);
   //uint32_t fileDataLen = recvPacket->len - sizeof(PacketHeader) - sizeof(uint32_t);

   //printf("RECEIVED DATA\n");

   //Get the packet #
   memcpy(&recvPackNum, recvPacket->buf + sizeof(PacketHeader), sizeof(int32_t));
   recvPackNum = ntohl(recvPackNum);
   //printf("Switching on packet num = %d\n", recvPackNum);
   //printf("Before: "); test_print_window();
      
   //ELSEIF packet # == num_SREJ //The last packet we SREJ'ed
   if (has_SREJ && recvPackNum == num_SREJ)
   {
      handle_expected_SREJ_packet(client, recvPacket, sendPacket);
   }

   //expected packet number
   else if (recvPackNum == cur_RRnum)
   {
      handle_expected_packet_number(client, recvPacket, sendPacket);
   }  
       
   //Client is in recovery mode
   else if (recvPackNum < cur_RRnum)
   {
      //printf("Client looks like its in recovery mode. same(recvPackNum = %d. win_min = %d)", recvPackNum, win_min);
      handle_client_recovery(client, recvPacket, sendPacket);
   }
   //We need to buffer this packet
   else if (recvPackNum > cur_RRnum)
   {
      buffer_packet(client, recvPacket, sendPacket, recvPackNum);
   }
   else
   {
      //printf("Unidentified packet number received\n");   
   }
   
   //printf("After: "); test_print_window();
   return state;
}


void handle_client_recovery(Connection *client, BufferChunk *recvPacket, BufferChunk *sendPacket)
{
   uint8_t response[1];
   
   if (!are_packets_buffered()) {
      //printf("handle_client_recovery() - Sending RR %d\n", cur_RRnum);
      send_buf_with_packet_num(response, 0, client, RR, seq_num++, sendPacket->buf, cur_RRnum);
   }
   else {
      //printf("handle_client_recovery() - Sending SREJ %d\n", cur_RRnum);
      send_buf_with_packet_num(response, 0, client, SREJ, seq_num++, sendPacket->buf, cur_RRnum);
   }
}

void handle_expected_SREJ_packet(Connection *client, BufferChunk *recvPacket, BufferChunk *sendPacket)
{
   uint8_t recvFileData[MAX_PACKET_LEN];
   memset(recvFileData, 0, MAX_PACKET_LEN);
   uint32_t fileDataLen = recvPacket->len - sizeof(PacketHeader) - sizeof(uint32_t) - 1;
   uint32_t nextPacketNum = -1;      //The next expected packet num after cleaning window buffer

   //Extract data and then append data to our file
   memcpy(recvFileData, recvPacket->buf + sizeof(PacketHeader) + sizeof(uint32_t), fileDataLen);

   if (write(fileFD, recvFileData, fileDataLen) < 0) {
      printf("received_data, write failed\n");
   }
   //printf("%s\n", recvFileData);
   
   //Allow server to send more SREJ packets
   has_SREJ = FALSE;

   cur_RRnum++; win_min++; win_max++; win_cur++;
   nextPacketNum = check_for_and_write_buffered_packets(cur_RRnum);
   send_RR_or_SREJ(client, sendPacket, nextPacketNum);      

   
}

void buffer_packet(Connection *client, BufferChunk *recvPacket, BufferChunk *sendPacket, int32_t recvPackNum)
{
   uint8_t response[1];
   uint8_t recvFileData[MAX_PACKET_LEN];
   memset(recvFileData, 0, MAX_PACKET_LEN);
   int32_t recvPackNumIndex = recvPackNum - 1;

   //IF packet # is in our window (should be...)
   if (is_in_window(recvPackNum)) {
      //printf("[][][][][][]Buffering data[][][][][][]\n");
      //Store packet in our buffer |window| at [packet # % win_size]
      memset(&window[recvPackNumIndex % win_size], 0, sizeof(BufferChunk));
      memcpy(&window[recvPackNumIndex % win_size], recvPacket, sizeof(BufferChunk));
   }      
   //ELSEIF packet # not in our window
   else {
      //printf("received_data, recvPackNum > cur_RRnum, This statement should never execute\n");
   }

   //Send a SREJ if we havent already
   if (!has_SREJ) {
      //Send SREJ on expected RR
      //printf("SREJ %d sent - buffer packet\n", cur_RRnum);
      send_buf_with_packet_num(response, 0, client, SREJ, seq_num++, sendPacket->buf, cur_RRnum);
      has_SREJ = TRUE;
      num_SREJ = cur_RRnum; //We dont have this packet :(
   }
   else {
      //printf("buffer_packet, Already sent SREJ %d\n", cur_RRnum);
   }

}

//win_min and win_max are indexes
//recvPackNum - 1 is the packet's index into our window
//recvPackNum 1 == win_min 0
uint8_t is_in_window(int32_t recvPackNum)
{
   uint8_t answer = FALSE;
   int32_t recvPackNumIndex = recvPackNum - 1;
   if (recvPackNumIndex >= win_min && recvPackNumIndex < win_max) {
      answer = TRUE;
   }
   return answer;
}

void handle_expected_packet_number(Connection *client, BufferChunk *recvPacket, BufferChunk *sendPacket)
{
   uint8_t recvFileData[MAX_PACKET_LEN];
   memset(recvFileData, 0, MAX_PACKET_LEN);
   uint32_t fileDataLen = recvPacket->len - sizeof(PacketHeader) - sizeof(uint32_t) - 1;
   uint32_t nextPacketNum = -1;      //The next expected packet num after cleaning window buffer

   //Extract data part of the packet
   //printf("fileDataLen = %d\n", fileDataLen);
   memcpy(recvFileData, recvPacket->buf + sizeof(PacketHeader) + sizeof(uint32_t), fileDataLen);

   //Append data part to our file
   if (write(fileFD, recvFileData, fileDataLen) < 0) {
      printf("received_data, write failed\n");
   }
   //printf("%s\n", recvFileData);

   cur_RRnum++; win_min++; win_max++; win_cur++;
   nextPacketNum = check_for_and_write_buffered_packets(cur_RRnum);
   send_RR_or_SREJ(client, sendPacket, nextPacketNum);      
}

void send_RR_or_SREJ(Connection *client, BufferChunk *sendPacket, uint32_t nextPacketNum)
{
   uint8_t response[1];
   //We have all packets and no packets are buffered
   if (cur_RRnum == nextPacketNum && !are_packets_buffered()) {
      //Send RR of cur_RR
      //printf("==>RR %d sent\n", cur_RRnum);
      send_buf_with_packet_num(response, 0, client, RR, seq_num++, sendPacket->buf, cur_RRnum);
   }
   //More packets are buffered and we need to SREJ for the missing packet if we
   //havnt sent a SREJ already
   else if (cur_RRnum > nextPacketNum && !has_SREJ) {
      //printf("cur_RRnum = %d, nextPacketNum = %d, has_SREJ = %d\n", cur_RRnum, nextPacketNum, has_SREJ);
      //Send SREJ of nextPacketNum
      //printf("==>SREJ %d sent\n", cur_RRnum);
      send_buf_with_packet_num(response, 0, client, SREJ, seq_num++, sendPacket->buf, cur_RRnum);
      has_SREJ = TRUE;
      num_SREJ = cur_RRnum; //We dont have this packet :(
   }
   //Do nothing
   else {
      //printf("send_RR_or_SREJ(), already sent SREJ %d and waiting for reply\n", cur_RRnum); 
   }
}

//Ret: next expected packet # to RR or SREJ
uint32_t check_for_and_write_buffered_packets(uint32_t curRR)
{  
   uint8_t data[MAX_PACKET_LEN];
   uint32_t dataLen = -1;
   uint32_t pos = curRR - 1;
   for(; (&window[pos % win_size])->len != -1; pos++)
   {
      //printf("[][][][][][]Writing Buffered Data[][][][][][]\n");
      memset(data, 0, MAX_PACKET_LEN);
      //Extract length of data in packet
      dataLen = (&window[pos % win_size])->len - sizeof(PacketHeader) - sizeof(uint32_t) - 1;
      //Extract data part of the packet
      memcpy(data, (&window[pos % win_size])->buf + sizeof(PacketHeader) + sizeof(uint32_t), dataLen);

      //Append data part to our file
      if (write(fileFD, data, dataLen) < 0) {
         printf("received_data, write failed\n");
      }
      //printf("%s\n", data);

      //Indicate that this window section is now free
      (&window[pos % win_size])->len = -1;
      cur_RRnum++; win_min++; win_max++; win_cur++;
   }
   return pos + 1;
}

//RET TRUE if there are packets buffered
uint8_t are_packets_buffered()
{
   uint8_t answer = FALSE;
   uint32_t pos = 0;
   for (; pos < win_size; pos++)
   {
      if ((&window[pos])->len != -1)
         answer = TRUE;     
   }
   return answer;
}

STATE ready_for_data_packet_setup(uint8_t *recvPacket, uint8_t *sendPacket)
{
   uint8_t i = 0;
   STATE state = RECEIVED_DATA;
   //Done reset recvPacket because this function is called when
   //we receive our first data packet
   memset(sendPacket, 0, MAX_PACKET_LEN);
   cur_RRnum = 1;
   win_min = 0;
   win_cur = 0;
   win_max = win_size;
   has_SREJ = FALSE;
   num_SREJ = 0;
   window = (BufferChunk *) calloc(win_size, sizeof(BufferChunk));
   
   //Set all BufferChunks in window as unused
   for (; i < win_size; i++) {
      (&window[i])->len = -1;
   }
   return state;
}



STATE ready_for_next_setup_packet(Connection *client, BufferChunk *recvPacket)//uint8_t *recvPacket)
{
   int32_t recv_len = 0;
   uint8_t flag = 0;
   STATE state = READY_FOR_NEXT_SETUP_PACKET;

   //Select(1)
   //If data is available
   if (select_call(client->sk_num, 1, 0, NOT_NULL) == 1)
   {
      timeout = 0;
      //Clear |recvPacket|
      memset(recvPacket, 0, MAX_PACKET_LEN);
      //Fill in |recvPacket| w/incoming data if the data is good
      if ((recv_len = recv_buf(MAX_PACKET_LEN, client->sk_num, client, &flag, &seq_num, recvPacket->buf)) != CRC_ERROR) {        
         if (flag == FILE_NAME) {
            //printf("RECEIVED FILE NAME\n");
            state = RECEIVED_REMOTE_FILE_NAME;
         }
         else if (flag  == BUFFER_SIZE) {
            //printf("RECEIVED BUFFER SIZE\n");
            state = RECEIVED_BUFFER_SIZE;
         }
         else if (flag == WINDOW_SIZE) {
            //printf("RECEIVED WINDOW SIZE\n");
            state = RECEIVED_WINDOW_SIZE;
         }
         else if (flag == DATA && fileGood && bufGood && winGood) {
            state = READY_FOR_DATA_PACKETS_SETUP;
            //Set the length of our recvPacket
            recvPacket->len = recv_len;
            //printf("RECEIVED OUR FIRST DATA PACKET!!!\n");
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
         state = TIMEOUT_ON_CLIENT;
      }
   }
   return state;
}


/* PRE: Packet has passed checksum
 */
STATE received_remote_file_name(Connection *client, uint8_t *recvPacket, int32_t recvPacketLen, uint8_t *sendPacket)
{
   uint8_t response[1];
   memset(sendPacket, 0, MAX_PACKET_LEN);

   //Retrieve the file name length
   uint32_t nameLen = 0;
   memcpy(&nameLen, recvPacket + sizeof(PacketHeader), sizeof(uint32_t));
   nameLen = ntohl(nameLen);
   
   //Retrieve the file name
   fileName = (int8_t *) calloc(1, nameLen + 1);
   memcpy(fileName, &recvPacket[sizeof(PacketHeader) + sizeof(uint32_t)], nameLen);
   //printf("fileName=%s\n", fileName);

   //Try to open the file if it is not already open
   if((fileFD < 0) && ((fileFD = open((const char *)fileName, O_CREAT | O_TRUNC | O_WRONLY, 0600)/*, S_IRWXU*/) < 0))
   {
      printf("Couldnt open file\n");
      //File is bad. Kill client process and tell client to exit
      send_buf(response, 0, client, REJ_FILE_NAME_NO_OPEN, seq_num++, sendPacket);
      return DONE;
   }
   //File can be open. Tell client to send Buffer Size by acking the file name
   //printf("-->SENDING FILE ACK\n");
   send_buf(response, 0, client, ACK_FILE_NAME, seq_num++, sendPacket);
   fileGood = TRUE;
   return READY_FOR_NEXT_SETUP_PACKET;
}

/* PRE: Packet has passed Checksum and buffer is known to be of
 * appropriate size because buffer size is checked on the client side.
 */
STATE received_buffer_size(Connection *client, uint8_t *recvPacket, int32_t recvPacketLen, uint8_t *sendPacket)
{
   uint8_t response[1];
   memset(sendPacket, 0, MAX_PACKET_LEN);

   //Retrieve the buffer size
   memcpy(&bufSize, &recvPacket[sizeof(PacketHeader)], sizeof(uint32_t));
   bufSize = ntohl(bufSize);
   //printf("BUFFER = %d\n", bufSize);
   //Tell client to send Window Size by acking the buffer size
   //printf("-->SENDING BUFFER ACK\n");
   send_buf(response, 0, client, ACK_BUFFER_SIZE, seq_num++, sendPacket);
   bufGood = TRUE;
   return READY_FOR_NEXT_SETUP_PACKET;

}

/* PRE: Packet has passed Checksum and buffer is known to be of
 * appropriate size because window size is checked on the client side.
 */
STATE received_window_size(Connection *client, uint8_t *recvPacket, int32_t recvPacketLen, uint8_t *sendPacket)
{
   uint8_t response[1];
   memset(sendPacket, 0, MAX_PACKET_LEN);

   //Retrieve the buffer size
   memcpy(&win_size, &recvPacket[sizeof(PacketHeader)], sizeof(uint32_t));
   win_size = ntohl(win_size);
   //printf("WINDOW = %d\n", win_size);
   //Tell client to start sending data by acking the window size
   //printf("-->SENDING WINDOW ACK\n");
   send_buf(response, 0, client, ACK_WINDOW_SIZE, seq_num++, sendPacket);
   winGood = TRUE;
   return READY_FOR_NEXT_SETUP_PACKET;
}

//Client only sends an eof if they are done transmitting
//and the last RR has been sent from the server
STATE received_eof(Connection *client, BufferChunk *recvPacket, BufferChunk *sendPacket)
{
   STATE state = DONE;
   uint8_t response[1];
   free(recvPacket);
   free(sendPacket);
   free(fileName);
   close(fileFD);
   free(window);
   send_buf(response, 0, client, ACK_EOF, seq_num++, sendPacket->buf);
   return state;
}

STATE timeout_on_client(BufferChunk *recvPacket, BufferChunk *sendPacket)
{
   STATE state = DONE;
   free(recvPacket);
   free(sendPacket);
   free(fileName);
   close(fileFD);
   free(window);
   return state;
}

STATE timeout_on_client_setup(BufferChunk *recvPacket, BufferChunk *sendPacket)
{
   STATE state = DONE;
   free(recvPacket);
   free(sendPacket);
   free(fileName);
   close(fileFD);
   return state;
}

/* usage: [error-percent] [optional-port-number]
 * |portNum| is filled in with optional-port-number OR Zero if no port number is given
 * |errRate| is filled in with error-percent
 */
void process_commandline_args(int argc, char *argv[], uint16_t *portNum, float *errRate)
{
   //Quit if there are too few or too many args
   if (argc <= 1 || argc >= 4)
   {
      printf("usage: %s [error-percent] [optional-port-number]\n", argv[0]);
      exit(-1);
   }
   //At least error-percent is provided
   if (argc >= 2)
   {
      //Error percentage [0, 1)
      if (atof(argv[1]) < 0 || atof(argv[1]) >= 1)
      {
         perror("Error percentage must be [0, 1)\n");
         exit(-1);
      }
      *errRate = atof(argv[1]);

      //Check for port number
      if (argc == 3)
      {
         *portNum = atoi(argv[2]);
      }
   }
}

void test_print_window()
{
   uint32_t pos = 0;
   uint32_t packNum = -1;
   printf("[");
   for (; pos < win_size; pos++)
   {
      if ((&window[pos])->len != -1)
      {
         memcpy(&packNum, (&window[pos])->buf + sizeof(PacketHeader), sizeof(uint32_t));
         printf("#%d - ", ntohl(packNum));
      }
      printf("%d", (&window[pos])->len);
      if (pos != win_size - 1)
         printf("|");
   }
   printf("]\n");
}


//READY FOR NEXT SETU PACKET
//Processes first accepted packet with a special exception
//Clears |packet|, selects, and receives/error checks file name, buffer
//    size and window size packets. 
//Throws out duplicate packets
//May set next state to:
//    READY_FOR_NEXT_SETUP_PACKET, TIMEOUT_ON_CLIENT
//    RECEIVED_ROMOTE_FILE_NAME, RECIEVED_BUFFER_SIZE, 
//    RECEIVED_WINDOW_SIZE, READY_FOR_DATA
//May only move to READY_FOR_DATA state once the file name, buffer
//    size, and the window size are all received and are okay

//ready for data packets setup
         //PRE: File name, Buffer size, and Window size are okay
         //Initializes window buffer for packets that come out of order
         //Will send: 
         //    FLAG_RR for Data Packet 1 THIS IS IMPLICIT WHEN ALL THE SETUP
         //    ACKS ARE RECEIVED!!
         //Will set next state to:
         //    READY_FOR_NEXT_DATA_PACKET

//Received Window Size
         //PRE: Received data is okay. 
         //Checks if the Window size is okay.
         //Saves window size if window size is okay
         //May send: 
         //    FLAG_ACK_WINDOW_SIZE, FLAG_REJ_WINDOW_SIZE
 
//Received EOF         
         //Close file
         //Send:
         //    FLAG_ACK_EOF
         //Set next state to:
         //    DONE


//Ready for next data packet
         //Clears |packet|, selects, and receives/error checks Data Packets
         //IF a setup packet is received --> send cur_RR and clear recv queue
         //Throws away duplicate packets
         //May set next state to:
         //    READY_FOR_NEXT_DATA_PACKET, TIMEOUT_ON_CLIENT
         //    RECEIVED_DATA, RECEIVED_EOF


//Received Data Packet
         //PRE: Received Data Packet is okay.
         //IF received not expected Data Packet and still w/in window --> buf
         //ELSE IF received SREJ'ed packet, --> write/flush buf up to next SREJ/RR
         //ELSE write to File
         //May send:
         //    FLAG_RR for Data Packet 1, 
         //    FLAG_RR for next Data Packet,
         //    FLAG_SREJ for lost Data Packet
         //May set next state to:
         //    READY_FOR_NEXT_DATA_PACKET

