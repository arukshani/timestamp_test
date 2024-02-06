/* 
 * udpclient.c - A simple UDP client
 * usage: udpclient <host> <port>
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
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h> 
#include <pthread.h>
#include <time.h>
#include <errno.h>

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

#define DEVICE "/dev/ptp2"

#include "common.h"
#include "hw_common.h"

#define BUFSIZE 1024

/* 
 * error - wrapper for perror
 */
void error(char *msg) {
    perror(msg);
    exit(0);
}

struct thread_data
{
  int  thread_id;
  int sockid;
};

int serverlen;
struct sockaddr_in serveraddr;
char readbuf[BUFSIZE];
int sockfd;

int main(int argc, char **argv) {
    int portno, n;
    
    // struct hostent *server;
    char *dst_hostip;
    char buf[BUFSIZE];

    /* check command line arguments */
    if (argc != 3) {
       fprintf(stderr,"usage: %s <dst_hostip> <port>\n", argv[0]);
       exit(0);
    }
    dst_hostip = argv[1];
    portno = atoi(argv[2]);

    /* socket: create the socket */
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) 
        error("ERROR opening socket");

    sockfd = setup_sock(sockfd);

    /* build the server's Internet address */
    bzero((char *) &serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_addr.s_addr = inet_addr(dst_hostip); 
    
    serveraddr.sin_port = htons(portno);

    serverlen = sizeof(serveraddr);

    // clkid = get_nic_clock_id();
    // pthread_t clock_thread;
    // pthread_create(&clock_thread, NULL, read_time, NULL);

    int m = 0;
    while(m < 5025)
    {
        // sleep(1);
        sleep(0.001);
        /* get a message from the user */
        bzero(buf, BUFSIZE);
        char snum[5];
        sprintf(snum, "%d", m);
        strcat(buf, snum);
        // sequence_ids[time_index] = m;
        

        /* send the message to the server */
        //send time
		// struct timespec client_send_time = get_nicclock();
        // send_timestamp_arr[time_index] = now;

        n = sendto(sockfd, buf, strlen(buf), 0, &serveraddr, serverlen);
        if (n < 0) 
            error("ERROR in sendto");

        rcv_xmit_tstamp(sockfd, m);
        m++;
        
        /* print the server's reply */
        // n = recvfrom(sockfd, buf, strlen(buf), 0, &serveraddr, &serverlen);
        // if (n < 0) 
        //     error("ERROR in recvfrom");

        //recv time
        // recv_timestamp_arr[time_index] = now;

        // time_index++;
    }

    // quit = 1;
    // pthread_join(clock_thread, NULL);

    int z = 0;
	FILE *fpt;
	fpt = fopen("./logs/nic-to-nic/exp2/nic-send-l2.csv", "w+");
	fprintf(fpt,"seq_id,time_part_sec,time_part_nsec\n");
	for (z = 0; z < time_index; z++ ) {
		fprintf(fpt,"%d,%ld,%ld\n",send_sequence_ids[z],send_timestamp_arr[z].tv_sec,send_timestamp_arr[z].tv_nsec);
	}
	fclose(fpt);

    return 0;
}