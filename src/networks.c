/*
 * networks.c
 *    By: Eric LaBouve (and Dr Smith)
 *
 *    Contains functions to support the setup, sending, and receiveing on UDP
 *    Sockets. 
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
#include "networks.h"

int32_t udp_server(uint16_t portNum)
{
   int32_t sk = 0;  //socket descriptor
   struct sockaddr_in local;  //socket address for us
   uint32_t len = sizeof(local); //length of local address

   //Create the socket
   if ((sk = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
   {
      perror("socket");
      exit(-1);
   }

   //Set up the socket
   local.sin_family = AF_INET;      //Internet family
   local.sin_addr.s_addr = INADDR_ANY;    //Wild card machine address
   local.sin_port = htons(portNum);     //Let the system choose the port

   //Bind the name (address) to a port
   if (bindMod(sk, (struct sockaddr *)&local, sizeof(local)) < 0)
   {
      perror("udp_server, bind");
      exit(-1);
   }

   //get the port name and print it out
   getsockname(sk, (struct sockaddr *)&local, &len);
   printf("Using Port #: %d\n", ntohs(local.sin_port));

   return sk;
}

//returns pointer to a sockaddr_in that it created or NULL if host not found
//Also passes back the socket number in sk
int32_t udp_client_setup(char *hostname, uint16_t port_num, Connection *connection)
{
   struct hostent *hp = NULL; //address of remote host

   connection->sk_num = 0;
   connection->len = sizeof(struct sockaddr_in);

   //create the socket
   if ((connection->sk_num = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
   {
      perror("udp_client_setup, socket");
      exit(-1);
   }

   //designate the addressing family
   connection->remote.sin_family = AF_INET;
   
   //get the address of the remote host and store
   hp = gethostbyname(hostname);

   if (hp == NULL)
   {
      printf("Host not found: %s\n", hostname);
      return -1;

   }
   
   memcpy(&(connection->remote.sin_addr), hp->h_addr, hp->h_length);

   //get the port used on the remote side and store
   connection->remote.sin_port = htons(port_num);

   return 0;
}
int32_t select_call(int32_t socket_num, int32_t seconds, int32_t microseconds, int32_t set_null)
{
   fd_set fdvar;
   struct timeval *timeout = NULL;

   if(set_null == NOT_NULL)
   {
      timeout = (struct timeval *) malloc(sizeof(struct timeval));
      timeout->tv_sec = seconds; //set timeout to 1 second
      timeout->tv_usec = microseconds; //set timeout to 0 micro seconds
   }

   FD_ZERO(&fdvar);
   FD_SET(socket_num, &fdvar);

   if (selectMod(socket_num + 1, (fd_set *)&fdvar, (fd_set *)0, (fd_set *)0, timeout) < 0)
   {
      perror("select");
      exit(-1);
   }

   if (FD_ISSET(socket_num, &fdvar))
   {
      return 1; //true
   } else {
      return 0; //false
   }
}

int32_t recv_buf(/*uint8_t *buf,*/ int32_t len, int32_t recv_sk_num, Connection *connection, uint8_t *flag, int32_t *seq_num, uint8_t *recv_packet) 
{
   char data_buf[MAX_PACKET_LEN];
   int32_t recv_len = 0;
   uint32_t remote_len = sizeof(struct sockaddr_in);
   PacketHeader *header;

   if ((recv_len = recvfrom(recv_sk_num, data_buf, len, 0, (struct sockaddr *)&(connection->remote), &remote_len)) < 0)
   {
      perror("recv_buf, recvfrom");
      exit(-1);
   }

   if (in_cksum((unsigned short *)data_buf, recv_len) != 0)
   {
      return CRC_ERROR;
   }
 
   header = (PacketHeader *)data_buf;   //Overlay our header on our packet
   *flag = header->type;   //Copy type of packet into our flag

   memcpy(seq_num, data_buf, sizeof(int32_t)); //Record our sequence number
   *seq_num = ntohl(*seq_num);
   
   //We received a large packet! Let's move the packet data into our buf
//   if (recv_len > sizeof(PacketHeader))
//   {      
      //NOT SURE WHY WE SUBTRACT 1. MAYBE TO EAT UP THE EOD CHARACTER (IF THERE
      //IS ONE?)
//      memcpy(buf, &data_buf[sizeof(PacketHeader)], recv_len - sizeof(PacketHeader) - 1);
//   }
    
   connection->len = remote_len;
   //Save our packet in recv_buffer
   memcpy(recv_packet, data_buf, recv_len);
   
   return recv_len;//(recv_len - sizeof(PacketHeader) - 1);
}

//PRE: recv_Packet is a Data packet
int32_t get_data_from_packet(int32_t recv_len, uint8_t *recv_packet, uint8_t *recv_data)
{
   uint8_t data_len = 0;
   //If the size of the packet holds data
   //[Header][Packet #][Data Len]...[Data]...
   if (recv_len > (sizeof(PacketHeader) + (2 * sizeof(uint32_t))))
   {
      data_len = recv_len - sizeof(PacketHeader) - (2 * sizeof(uint32_t)) - 1;
      memcpy(recv_data, &recv_packet[sizeof(PacketHeader) + (2 * sizeof(uint32_t))], data_len);     
   }
   return data_len;
}

int32_t send_buf(uint8_t *buf, int32_t len, Connection *connection, uint8_t flag, uint32_t seq_num, uint8_t *packet)
{
   int32_t send_len = 0;
   uint16_t checksum = 0;

   /*set up the packet (seq#, crc, flag, data)*/
   if (len > 0)
   {
      memcpy(&packet[sizeof(PacketHeader)], buf, len);
   }
   seq_num = htonl(seq_num);
   memcpy(&packet[0], &seq_num, sizeof(uint32_t));
   packet[sizeof(uint32_t) + sizeof(uint16_t)] = flag;

   memset(&packet[sizeof(uint32_t)], 0, sizeof(uint16_t));
   
   checksum = in_cksum((unsigned short *) packet, len + 8);

   memcpy(&packet[4], &checksum, 2);

   if ((send_len = sendtoErr(connection->sk_num, packet, len + 8, 0, (struct sockaddr *)&(connection->remote), connection->len)) < 0)
   {
      perror("send_buf, sendto");
      exit(-1);
   }

   return send_len;
}

int32_t send_buf_with_packet_num(uint8_t *buf, int32_t len, Connection *connection, uint8_t flag, uint32_t seq_num, uint8_t *packet, uint32_t packet_num)
{
   int32_t send_len = 0;
   uint16_t checksum = 0;

   /*set up the packet (seq#, crc, flag, packet #, data)*/
   if (len > 0)
   {  //Copy file data
      memcpy(&packet[sizeof(PacketHeader) + sizeof(uint32_t)], buf, len);
   }
   //Copy seq #
   seq_num = htonl(seq_num);
   memcpy(&packet[0], &seq_num, sizeof(uint32_t));
   //Copy flag
   packet[sizeof(uint32_t) + sizeof(uint16_t)] = flag;
   //Copy packet #
   packet_num = htonl(packet_num);
   memcpy(&packet[sizeof(PacketHeader)], &packet_num, sizeof(uint32_t));
   
   memset(&packet[sizeof(uint32_t)], 0, sizeof(uint16_t));
   
   checksum = in_cksum((unsigned short *) packet, len + 8 + sizeof(uint32_t));

   memcpy(&packet[4], &checksum, 2);

   if ((send_len = sendtoErr(connection->sk_num, packet, len + 8 + sizeof(uint32_t), 0, (struct sockaddr *)&(connection->remote), connection->len)) < 0)
   {
      perror("send_buf_with_packet_num, sendto");
      exit(-1);
   }

   return send_len;
}

//Returns the length of the data extracted
/*int32_t get_data_from_packet(uint8_t *recv_packet, int32_t packet_len, uint8_t *data)
{
   if (packet_len > sizeof(PacketHeader))
      memcpy(&(recv_packet[sizeof(PacketHeader)]), data, packet_len - sizeof(PacketHeader) - 1);
   return (packet_len - sizeof(PacketHeader) - 1);
}
*/
