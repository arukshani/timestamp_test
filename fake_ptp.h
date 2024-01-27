#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef SIOCSHWTSTAMP
# define SIOCSHWTSTAMP 0x89b0
#endif

#ifndef CLOCK_INVALID
#define CLOCK_INVALID -1
#endif

#define SEQ_OFFSET 44
#define DOMAIN_NUM_OFFSET 18

#define ERR(str, ...) fprintf(stderr, "Error: "str, ##__VA_ARGS__)

#define _DEBUG(file, fmt, ...) do { \
	if (debugen) { \
		fprintf(file, " " fmt, \
		##__VA_ARGS__); \
	} else { \
		; \
	} \
} while (0)

#define DEBUG(...) _DEBUG(stderr, __VA_ARGS__)

// extern bool debugen;// = false;
extern bool running;// = true;

struct pkt_time {
	__u16 seq;
	struct timespec xmit;
	struct timespec recv;
};

typedef struct packets {
	pthread_mutex_t list_lock;
	struct pkt_time *list;
	int list_start;
	int list_head;
	int list_len;
	__u16 next_seq;
	bool txcount_flag;
	unsigned char *frame;
	size_t frame_size;
	int timerfd;
} Packets;
