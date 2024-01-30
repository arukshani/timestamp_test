//Original is from https://github.com/cappe987/wiretime.git


/*
 * MIT License

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
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

#include "fake_ptp.h"

#define DOMAIN_NUM 0xCA

bool debugen = true;
bool running = true;

long time_index = 0;
struct timespec timestamp_arr[2000];
__u16 sequence_ids[2000];

static unsigned char sync_packet[] = {
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, // dmac
	0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, // smac
	0x88, 0xf7, // PTPv2 ethertype
	0x00, // majorSdoId, messageType (0x0=Sync)
	0x02, // minorVersionPtp, versionPTP
	0x00, 0x2c, // messageLength
	DOMAIN_NUM, // domainNumber (use domain number 0xff for this)
	0x00, // majorSdoId
	0x02, 0x00, // flags
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // correctionField
	0x00, 0x00, 0x00, 0x00, // messageTypeSpecific
	0xbb, 0xbb, 0xbb, 0xff, 0xfe, 0xbb, 0xbb, 0xbb, // clockIdentity
	0x00, 0x01, // sourcePort
	0x00, 0x01, // sequenceId
	0x00, // controlField
	0x00, // logMessagePeriod
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // originTimestamp (seconds)
	0x00, 0x00, 0x00, 0x00, // originTimestamp (nanoseconds)
};

static void bail(const char *error)
{
	printf("%s: %s\n", error, strerror(errno));
	exit(1);
}

static __u16 char_to_u16(unsigned char a[]) {
	__u16 n = 0;
	memcpy(&n, a, 2);
	return n;
}

static void sig_handler(int sig)
{
	running = false;
}

static void debug_packet_data(size_t length, uint8_t *data)
{
	size_t i;

	if (!debugen)
		return;

	DEBUG("Length %ld\n", length);
	if (length > 0) {
		fprintf(stderr, " ");
		for (i = 0; i < length; i++)
			fprintf(stderr, "%02x ", data[i]);
		fprintf(stderr, "\n");
	}
}

/* Checks that the packet received was actually one sent by us */
static int is_rx_tstamp(unsigned char *buf)
{
	// Will arive without tag when received on the socket
	/*if (using_tagged())*/
		/*return buf[DOMAIN_NUM_OFFSET + VLAN_TAG_SIZE] == DOMAIN_NUM;*/
	/*else*/
		return buf[DOMAIN_NUM_OFFSET] == DOMAIN_NUM;
}

static clockid_t get_clockid(int fd)
{
#define CLOCKFD 3
	return (((unsigned int) ~fd) << 3) | CLOCKFD;
}

static void setsockopt_txtime(int fd)
{

	// int fdd;
    // char *device = "/dev/ptp2";
    // clockid_t clkid;

    // fdd = open(device, O_RDWR);
	// if (fdd < 0) {
	// 	fprintf(stderr, "opening %s: %s\n", device, strerror(errno));
	// 	// return -1;
	// }

	// clkid = get_clockid(fdd);
	// if (CLOCK_INVALID == clkid) {
	// 	fprintf(stderr, "failed to read clock id\n");
	// 	// return -1;
	// }

	struct sock_txtime so_txtime_val = {
			.clockid =  CLOCK_TAI,
			// .clockid = clkid,
			/*.flags = SOF_TXTIME_DEADLINE_MODE | SOF_TXTIME_REPORT_ERRORS */
			.flags = SOF_TXTIME_REPORT_ERRORS
			};
	struct sock_txtime so_txtime_val_read = { 0 };
	socklen_t vallen = sizeof(so_txtime_val);

	if (setsockopt(fd, SOL_SOCKET, SO_TXTIME,
		       &so_txtime_val, sizeof(so_txtime_val)))
		printf("setsockopt txtime error!\n");

	if (getsockopt(fd, SOL_SOCKET, SO_TXTIME,
		       &so_txtime_val_read, &vallen))
		printf("getsockopt txtime error!\n");

	if (vallen != sizeof(so_txtime_val) ||
	    memcmp(&so_txtime_val, &so_txtime_val_read, vallen))
		printf("getsockopt txtime: mismatch\n");
}

static void setup_hwconfig(char *interface, int sock, int st_tstamp_flags,
			   bool ptp_only, bool one_step)
{
	struct hwtstamp_config hwconfig, hwconfig_requested;
	struct ifreq hwtstamp;

	/* Set the SIOCSHWTSTAMP ioctl */
	memset(&hwtstamp, 0, sizeof(hwtstamp));
	strncpy(hwtstamp.ifr_name, interface, sizeof(hwtstamp.ifr_name));
	hwtstamp.ifr_data = (void *)&hwconfig;
	memset(&hwconfig, 0, sizeof(hwconfig));

	if (st_tstamp_flags & SOF_TIMESTAMPING_TX_HARDWARE) {
		if (one_step)
			hwconfig.tx_type = HWTSTAMP_TX_ONESTEP_SYNC;
		else
			hwconfig.tx_type = HWTSTAMP_TX_ON;
	} else {
		hwconfig.tx_type = HWTSTAMP_TX_OFF;
	}
	if (ptp_only)
		hwconfig.rx_filter =
			(st_tstamp_flags & SOF_TIMESTAMPING_RX_HARDWARE) ?
			HWTSTAMP_FILTER_PTP_V2_SYNC : HWTSTAMP_FILTER_NONE;
	else
		hwconfig.rx_filter =
			(st_tstamp_flags & SOF_TIMESTAMPING_RX_HARDWARE) ?
			HWTSTAMP_FILTER_ALL : HWTSTAMP_FILTER_NONE;

	hwconfig_requested = hwconfig;
	if (ioctl(sock, SIOCSHWTSTAMP, &hwtstamp) < 0) {
		if ((errno == EINVAL || errno == ENOTSUP) &&
		    hwconfig_requested.tx_type == HWTSTAMP_TX_OFF &&
		    hwconfig_requested.rx_filter == HWTSTAMP_FILTER_NONE) {
			printf("SIOCSHWTSTAMP: disabling hardware time stamping not possible\n");
			exit(1);
		}
		else {
			printf("SIOCSHWTSTAMP: operation not supported!\n");
			exit(1);
		}
	}
	printf("SIOCSHWTSTAMP: tx_type %d requested, got %d; rx_filter %d requested, got %d\n",
	       hwconfig_requested.tx_type, hwconfig.tx_type,
	       hwconfig_requested.rx_filter, hwconfig.rx_filter);
}

int setup_sock(char *interface, int prio, int st_tstamp_flags,
	       bool ptp_only, bool one_step, bool software_ts)
{
	struct sockaddr_ll addr;
	struct ifreq device;
	socklen_t len;
	int sock;
	int val;

	sock = socket(PF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
	if (sock < 0)
		bail("socket");

	memset(&device, 0, sizeof(device));
	strncpy(device.ifr_name, interface, sizeof(device.ifr_name));
	if (ioctl(sock, SIOCGIFINDEX, &device) < 0)
		bail("getting interface index");

	if (!software_ts) {
		setup_hwconfig(interface, sock, st_tstamp_flags, ptp_only, one_step);
	}

	/* bind to PTP port */
	addr.sll_ifindex = device.ifr_ifindex;
	addr.sll_family = AF_PACKET;
	addr.sll_protocol = htons(ETH_P_ALL);
	addr.sll_pkttype = PACKET_BROADCAST;
	addr.sll_hatype   = ARPHRD_ETHER;
	memset(addr.sll_addr, 0, 8);
	addr.sll_halen = 0;
	if (bind(sock, (struct sockaddr*) &addr, sizeof(struct sockaddr_ll)) < 0)
		bail("bind");
	if (setsockopt(sock, SOL_SOCKET, SO_BINDTODEVICE, interface, strlen(interface)))
		bail("setsockopt SO_BINDTODEVICE");
	if (setsockopt(sock, SOL_SOCKET, SO_PRIORITY, &prio, sizeof(int)))
		bail("setsockopt SO_PRIORITY");

	if (st_tstamp_flags &&
	    setsockopt(sock, SOL_SOCKET, SO_TIMESTAMPING,
		       &st_tstamp_flags, sizeof(st_tstamp_flags)) < 0)
		printf("setsockopt SO_TIMESTAMPING not supported\n");

	/* verify socket options */
	len = sizeof(val);

	if (getsockopt(sock, SOL_SOCKET, SO_TIMESTAMPING, &val, &len) < 0) {
		printf("%s: %s\n", "getsockopt SO_TIMESTAMPING", strerror(errno));
	} else {
		// DEBUG("SO_TIMESTAMPING %d\n", val);
		if (val != st_tstamp_flags)
			printf("   not the expected value %d\n", st_tstamp_flags);
	}

	setsockopt_txtime(sock);

	return sock;
}


int setup_tx_sock(char *iface, int prio, bool ptp_only, bool one_step, bool software_ts)
{
	int so_tstamp_flags = 0;

	if (software_ts) {
		so_tstamp_flags |= (SOF_TIMESTAMPING_TX_SOFTWARE | SOF_TIMESTAMPING_OPT_TSONLY);
		so_tstamp_flags |= (SOF_TIMESTAMPING_RX_SOFTWARE | SOF_TIMESTAMPING_OPT_CMSG);
		so_tstamp_flags |= SOF_TIMESTAMPING_SOFTWARE;
	} else {
		so_tstamp_flags |= (SOF_TIMESTAMPING_TX_HARDWARE | SOF_TIMESTAMPING_OPT_TSONLY);
		so_tstamp_flags |= (SOF_TIMESTAMPING_RX_HARDWARE | SOF_TIMESTAMPING_OPT_CMSG);
		so_tstamp_flags |= SOF_TIMESTAMPING_RAW_HARDWARE;
	}

	return setup_sock(iface, prio, so_tstamp_flags, ptp_only, one_step, software_ts);
}

int get_smac(int sockfd, char ifname[IFNAMSIZ], unsigned char smac[6])
{
	struct ifreq buffer = { 0 };
	memcpy(buffer.ifr_ifrn.ifrn_name, ifname, IFNAMSIZ);

	if (ioctl(sockfd, SIOCGIFHWADDR, &buffer) < 0) {
		/* FIXME: Virtual hardware interfaces cannot use SIOCGIFHWADDR.
		 * Maybe use the `ip link` interface?
		 */
		memcpy(smac, "\xAA\xAA\xAA\xAA\xAA\xAA", ETH_ALEN);
		return 0;
		printf("smac %2X\n", buffer.ifr_hwaddr.sa_data[0]);
		perror("Error");
		ERR("Unable to find source MAC\n");
		return -ENOENT;
	}
	memcpy(smac, (buffer.ifr_hwaddr.sa_data), ETH_ALEN);
	return 0;
}

void set_smac(unsigned char *frame, unsigned char mac[ETH_ALEN])
{
	for (int i = 0; i < ETH_ALEN; i++)
		frame[6 + i] = mac[i];
}

static int create_timer()
{
	struct itimerspec timer;
	int interval = 1000;

	int fd = timerfd_create(CLOCK_REALTIME, 0);
	if (fd < 0)
		return fd;
	timer.it_value.tv_sec = interval / 1000;
	timer.it_value.tv_nsec = (interval % 1000) * 1000000;
	timer.it_interval.tv_sec = interval / 1000;
	timer.it_interval.tv_nsec = (interval % 1000) * 1000000;

	timerfd_settime(fd, 0, &timer, NULL);
	return fd;
}


static void u16_to_char(unsigned char a[], __u16 n) {
	memcpy(a, &n, 2);
}

static void set_sequenceId(unsigned char *packet, __u16 seq_id)
{
	/* Convert to Big Endian so sequenceId looks correct when viewed in
	 * Wireshark or Tcpdump.
	 */
	// seq_id = htons(seq_id);
	// if (using_tagged(cfg))
	// 	u16_to_char(&packet[SEQ_OFFSET + VLAN_TAG_SIZE], seq_id);
	// else
	// 	u16_to_char(&packet[SEQ_OFFSET], seq_id);

	seq_id = htons(seq_id);
	u16_to_char(&packet[SEQ_OFFSET], seq_id);
}

static void pkts_append_seq(Packets *pkts, __u16 tx_seq)
{
	pthread_mutex_lock(&pkts->list_lock);
	pkts->list[tx_seq % pkts->list_len].seq = tx_seq;
	pkts->list_head = (pkts->list_head + 1) % pkts->list_len;
	pthread_mutex_unlock(&pkts->list_lock);
}

__u16 prepare_packet(Packets *pkts)
{
	__u16 tx_seq;

	tx_seq = pkts->next_seq;
	pkts->next_seq++;
	// DEBUG("Xmit: %u\n", tx_seq);

	set_sequenceId(pkts->frame, tx_seq);

	pkts_append_seq(pkts, tx_seq);

	return tx_seq;
}

static __u16 sendpacket(int sock, unsigned int length, Packets *pkts)
{
	__u16 tx_seq;

	tx_seq = prepare_packet(pkts);

	send(sock, pkts->frame, pkts->frame_size, 0);

	return tx_seq;
}

void get_timestamp(struct msghdr *msg, struct timespec **stamp, int recvmsg_flags, Packets *pkts)
{
	struct sockaddr_in *from_addr = (struct sockaddr_in *)msg->msg_name;
	struct cmsghdr *cmsg;

	for (cmsg = CMSG_FIRSTHDR(msg); cmsg; cmsg = CMSG_NXTHDR(msg, cmsg)) {
		switch (cmsg->cmsg_level) {
		case SOL_SOCKET:
			switch (cmsg->cmsg_type) {
			case SO_TIMESTAMPING: {
				*stamp = (struct timespec *)CMSG_DATA(cmsg);
				/* stamp is an array containing 3 timespecs:
				 * SW, HW transformed, HW raw.
				 * Use SW or HW raw
				 */
				// if (!cfg->software_ts) {
				// 	/* skip SW */
				// 	(*stamp)++;
				// 	/* skip deprecated HW transformed */
				// 	(*stamp)++;
				// }
				/* skip SW */
				(*stamp)++;
				/* skip deprecated HW transformed */
				(*stamp)++;

				if (recvmsg_flags & MSG_ERRQUEUE)
					pkts->txcount_flag = 1;
				break;
			}
			default:
				/*DEBUG("type %d\n", cmsg->cmsg_type);*/
				break;
			}
			break;
		default:
			/*DEBUG("level %d type %d\n",*/
				/*cmsg->cmsg_level,*/
				/*cmsg->cmsg_type);*/
			break;
		}
	}
}

static __u16 get_sequenceId(unsigned char *buf)
{
	// Will arive without tag when received on the socket
	/*if (using_tagged())*/
		/*return char_to_u16(&buf[SEQ_OFFSET + VLAN_TAG_SIZE]);*/
	/*else*/
		return ntohs(char_to_u16(&buf[SEQ_OFFSET]));
}

void save_tstamp(struct timespec *stamp, unsigned char *data, size_t length, Packets *pkts, __s32 tx_seq, int recvmsg_flags)
{
	struct timespec one_step_ts;
	__u16 pkt_seq;
	int idx;
	int got_tx = 0;
	int got_rx = 0;

	// debug_packet_data(length, data);

	if (tx_seq >= 0) {
		got_tx = 1;
		pkt_seq = tx_seq;
		DEBUG("Got TX seq %d. %lu.%lu\n", pkt_seq, stamp->tv_sec, stamp->tv_nsec);
		timestamp_arr[time_index].tv_sec = stamp->tv_sec;
		timestamp_arr[time_index].tv_nsec = stamp->tv_nsec;
		sequence_ids[time_index] = pkt_seq;
		time_index++;
	} else if (is_rx_tstamp(data)) {
		got_rx = 1;
		pkt_seq = get_sequenceId(data);
		DEBUG("Got RX seq %d. %lu.%lu\n", pkt_seq, stamp->tv_sec, stamp->tv_nsec);
	} else {
		// If the packet we read was not the tx packet then set back
		// txcount_flag and try again.
		if (recvmsg_flags & MSG_ERRQUEUE)
			pkts->txcount_flag = 0;
		return;
	}

	idx = pkt_seq % pkts->list_len;

	// if (got_rx && cfg->one_step)
	// 	one_step_ts = get_one_step(pkts, idx, data, pkt_seq);

	// if (!false) {
	// 	int has_first = 1;
	// 	cfg->first_tstamp = cfg->one_step ? one_step_ts : *stamp;
	// }

	pthread_mutex_lock(&pkts->list_lock);
	if (pkts->list[idx].seq == pkt_seq) {
		if (got_tx)
			pkts->list[idx].xmit = *stamp;
		else if (got_rx)
			pkts->list[idx].recv = *stamp;
	}
	pthread_mutex_unlock(&pkts->list_lock);
}

static void recvpacket(int sock, int recvmsg_flags, Packets *pkts,
		       __s32 tx_seq)
{
	char data[256];
	struct msghdr msg;
	struct iovec entry;
	struct sockaddr_in from_addr;
	struct timespec *stamp = NULL;
	struct {
		struct cmsghdr cm;
		char control[512];
	} control;
	int res;

	memset(&msg, 0, sizeof(msg));
	msg.msg_iov = &entry;
	msg.msg_iovlen = 1;
	entry.iov_base = data;
	entry.iov_len = sizeof(data);
	memset(data, 0, sizeof(data));
	msg.msg_name = (caddr_t)&from_addr;
	msg.msg_namelen = sizeof(from_addr);
	msg.msg_control = &control;
	msg.msg_controllen = sizeof(control);

	res = recvmsg(sock, &msg, recvmsg_flags | MSG_DONTWAIT);
	if (res >= 0) {
		// printf("IN recvpacket \n");
		get_timestamp(&msg, &stamp, recvmsg_flags, pkts);
		if (!stamp)
			return;
		save_tstamp(stamp, msg.msg_iov->iov_base,
			    res, pkts, tx_seq, recvmsg_flags);
	}
}

void rcv_xmit_tstamp(int sock, Packets *pkts, __u16 tx_seq) {
	fd_set readfs, errorfs;
	int res;

	// printf("rcv_xmit_tstamp\n");
	while (!pkts->txcount_flag) {
		// printf("IN rcv_xmit_tstamp\n");
		FD_ZERO(&readfs);
		FD_ZERO(&errorfs);
		FD_SET(sock, &readfs);
		FD_SET(sock, &errorfs);

		res = select(sock + 1, &readfs, 0, &errorfs, NULL);
		if (res > 0) {
			// printf("IN rcv_xmit_tstamp\n");
			// recvpacket(sock, 0, pkts, tx_seq);
			recvpacket(sock, MSG_ERRQUEUE, pkts, tx_seq);
		}
	}

	return;
}

void do_xmit(Packets *pkts, int sock)
{
	int delay_us = 1000 * 1000;
	int length = 0;
	__u16 tx_seq;

	 /*write one packet */
	tx_seq = sendpacket(sock, length, pkts);
	pkts->txcount_flag = 0;

	 /* Receive xmit timestamp for packet */
	// if (!cfg->one_step)
		// rcv_xmit_tstamp(sock, cfg, pkts, tx_seq);

	rcv_xmit_tstamp(sock, pkts, tx_seq);
}

static int run(Packets *pkts, int tx_sock)
{
	/* Count to ensure a whole batch has been sent when the time is reached */
	int current_batch = 0;
	__u64 triggers = 0;
	char dummybuf[8];
	int retval = 0;
	fd_set rfds;
	int start;
	int end;

	/* Watch timerfd file descriptor */
	FD_ZERO(&rfds);
	FD_SET(pkts->timerfd, &rfds);

	/* Main loop */
	printf("Transmitting...\n");
	while (running) {
		retval = select(pkts->timerfd + 1, &rfds, NULL, NULL, NULL); /* Last parameter = NULL --> wait forever */
		if (retval < 0 && errno == EINTR) {
			retval = 0;
			break;
		}
		if (retval < 0) {
			perror("Error");
			break;
		}
		if (retval == 0) {
			continue;
		}

		if (FD_ISSET(pkts->timerfd, &rfds))
			read(pkts->timerfd, dummybuf, 8);

		do_xmit(pkts, tx_sock);
	}
}



int main(int argc, char **argv)
{
    int tx_sock;
	int err;
    unsigned char mac[ETH_ALEN];

    Packets pkts;
    memset(&pkts, 0, sizeof(Packets));

    pkts.txcount_flag = false;
	pkts.list_head = 0;
	pkts.next_seq = 0;
    pkts.frame = sync_packet;
	pkts.frame_size = sizeof(sync_packet);

	signal(SIGINT, sig_handler);
    pthread_mutex_init(&pkts.list_lock, NULL);

    pkts.list_len = 65536;
    pkts.list = calloc(sizeof(struct pkt_time), pkts.list_len);
	if (!pkts.list)
		return ENOMEM;

    /* Sender */
    tx_sock = setup_tx_sock("enp24s0np0", 0, true, false, false);
    if (tx_sock < 0) {
		ERR("failed setting up TX socket\n");
		// goto out_err_tx_sock;
	}

    get_smac(tx_sock, "enp24s0np0", mac);
	set_smac(pkts.frame, mac);

	pkts.timerfd = create_timer();
	if (pkts.timerfd < 0)
		goto out_err_timer;

	err = run(&pkts, tx_sock);

	int z = 0;
	FILE *fpt;
	fpt = fopen("./wire_to_wire_send.csv", "w+");
	fprintf(fpt,"seq_id,time_part_sec,time_part_nsec\n");
	for (z = 0; z < time_index; z++ ) {
		fprintf(fpt,"%d,%ld,%ld\n",sequence_ids[z],timestamp_arr[z].tv_sec,timestamp_arr[z].tv_nsec);
	}
	fclose(fpt);

// out_err_tx_sock:
	// pthread_kill(rx_thread, SIGINT);
	// close(rx_args.sockfd);
	// return err;
out_err_timer:
	close(tx_sock);

    free(pkts.list);
}



