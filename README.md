# SlidingWindowProtocol
Client and Server uses UDP protocol to allow a dynamic number of clients to upload a file to the server

Eric LaBouve

INSTRUCTIONS ON HOW TO RUN THE PROGRAM:

To build and link all files, type:
> make

To Run rcopy (The client program):
> rcopy local-file remote-file buffer-size error-percent window-size remote-machine remote-port

where:
local-file: is the file to send from rcopy to server
remote-file: is the name of file the server should use to store the sent file
buffer-size: is the number of data bytes (from the file) transmitted in a data packet1
error-percent: is the percent of packets that are in error (floating point number)
window-size: is the size of the window in PACKETS (i.e. number of packets in window)
remote-machine: is the remote machine running the server
remote-port: is the port number of the server application 

To Run server:
> server error-percent optional-port-number 

where:
error-percent: is the percent of packets that are in error (floating point number)
optional-port-number: allows the user to specify the port number to be used by the server
   (If this parameter is not present you should pass a 0 (zero) as the port
   number to bind().) 

Note: The server will receive the window-size from rcopy.




FLAG MEANINGS:

0   INVALID_FLAG0,
1   DATA, //C --> S
2   INVALID_FLAG2,
3   RR,   //S --> C
4   SREJ, //S --> C
5   INVALID_FLAG5,

6   FILE_NAME,     //C --> S
7   ACK_FILE_NAME, //S --> C Remote file name is good
8   REJ_FILE_NAME, //S --> C Not used
9   REJ_FILE_NAME_NO_OPEN,  //S --> C
   
10   BUFFER_SIZE,      //C --> S 
11   ACK_BUFFER_SIZE,  //S --> C Buffer size is good
12   REJ_BUFFER_SIZE,  //S --> C Not used

13   WINDOW_SIZE,      //C --> S
14   ACK_WINDOW_SIZE,  //S --> C Window size is good
15   REJ_WINDOW_SIZE,  //S --> C Not used

16   END_OF_FILE,      //C --> S 
17   ACK_EOF,          //S --> C
18   CRC_ERROR = -1

Where:
C = Client
S = Server
