/* 
 * udpserver.c - A simple UDP echo server 
 * usage: udpserver <port>
 */

 #include <stdio.h>
#include <stdbool.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <signal.h>
#include <getopt.h>
#include <sys/timerfd.h>
#include <sys/eventfd.h>
#include <fcntl.h>

#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <asm/socket.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <linux/if_arp.h>

#include <asm/types.h>

#include <linux/net_tstamp.h>

#include <linux/if_ether.h>



#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <linux/ptp_clock.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <sys/errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdbool.h>
#include <signal.h>
#include <pthread.h>

#define DEVICE "/dev/ptp2"

#include "common.h"
#include "hw_common.h"

#define BUFSIZE 1024

/*
 * error - wrapper for perror
 */
void error(char *msg) {
  perror(msg);
  exit(1);
}

// extern bool running;// = true;
// bool running = true;

// static void sig_handler(int sig)
// {
// 	running = false;
// }

// long time_index = 0;
// struct timespec send_timestamp_arr[2000];
// struct timespec recv_timestamp_arr[2000];
// int sequence_ids[20000];

static void sig_handler(int sig)
{
	running = false;
}

int main(int argc, char **argv) {
  signal(SIGINT, sig_handler);

  int sockfd; /* socket */
  int portno; /* port to listen on */
  int clientlen; /* byte size of client's address */
  struct sockaddr_in serveraddr; /* server's addr */
  struct sockaddr_in clientaddr; /* client addr */
  struct hostent *hostp; /* client host info */
  char buf[BUFSIZE]; /* message buf */
  char *hostaddrp; /* dotted decimal host addr string */
  int optval; /* flag value for setsockopt */
  int n; /* message byte size */

  /* 
   * check command line arguments 
   */
  if (argc != 2) {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }
  portno = atoi(argv[1]);

  /* 
   * socket: create the parent socket 
   */
  sockfd = socket(AF_INET, SOCK_DGRAM, 0);
  if (sockfd < 0) 
    error("ERROR opening socket");

  sockfd = setup_sock(sockfd);

  /* setsockopt: Handy debugging trick that lets 
   * us rerun the server immediately after we kill it; 
   * otherwise we have to wait about 20 secs. 
   * Eliminates "ERROR on binding: Address already in use" error. 
   */
  optval = 1;
  setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, 
	     (const void *)&optval , sizeof(int));

  /*
   * build the server's Internet address
   */
  bzero((char *) &serveraddr, sizeof(serveraddr));
  serveraddr.sin_family = AF_INET;
  serveraddr.sin_port = htons((unsigned short)portno);

  /* 
   * bind: associate the parent socket with a port 
   */
  if (bind(sockfd, (struct sockaddr *) &serveraddr, 
	   sizeof(serveraddr)) < 0) 
    error("ERROR on binding");

  /* 
   * main loop: wait for a datagram, then echo it
   */
  clientlen = sizeof(clientaddr);
  // clkid = get_nic_clock_id();
  // pthread_t clock_thread;
  // pthread_create(&clock_thread, NULL, read_time, NULL);

  // pthread_t rx_thread;
  // struct rx_thread_data rx_args;
  // rx_args.sockfd = sockfd;
  // pthread_create(&rx_thread, NULL, rcv_pkt, &rx_args);

  // while (time_index < 10) {

    // bzero(buf, BUFSIZE);
    // n = recvfrom(sockfd, buf, BUFSIZE, 0,
		//  (struct sockaddr *) &clientaddr, &clientlen);
    // if (n < 0)
    //   error("ERROR in recvfrom");

    // printf("Main thread %d \n", atoi(buf));
    

  // }

  rcv_pkt(sockfd);
  printf("hello \n");
  // running = false;
  // pthread_join(rx_thread, NULL);

    // quit = 1;
    // pthread_join(clock_thread, NULL);

    int z = 0;
	FILE *fpt;
	fpt = fopen("./logs/nic-to-nic/exp2/nic-recv-l1.csv", "w+");
	fprintf(fpt,"seq_id,time_part_sec,time_part_nsec\n");
	for (z = 0; z < time_index; z++ ) {
		fprintf(fpt,"%d,%ld,%ld\n",recv_sequence_ids[z],recv_timestamp_arr[z].tv_sec,recv_timestamp_arr[z].tv_nsec);
	}
	fclose(fpt);

}