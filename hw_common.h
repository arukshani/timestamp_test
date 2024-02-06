
char *interface = "enp24s0np0";

#ifndef SIOCSHWTSTAMP
# define SIOCSHWTSTAMP 0x89b0
#endif

long time_index = 0;
struct timespec send_timestamp_arr[20000];
struct timespec recv_timestamp_arr[20000];
int send_sequence_ids[20000];
int recv_sequence_ids[20000];

bool running = true;

struct rx_thread_data {
	int sockfd;
};

static void bail(const char *error)
{
	printf("%s: %s\n", error, strerror(errno));
	exit(1);
}

static void setsockopt_txtime(int fd)
{
    struct sock_txtime so_txtime_val = {
			.clockid =  CLOCK_TAI,
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

static void setup_hwconfig(char *interface, int sock, int st_tstamp_flags)
{
    struct hwtstamp_config hwconfig, hwconfig_requested;
	struct ifreq hwtstamp;

    /* Set the SIOCSHWTSTAMP ioctl */
	memset(&hwtstamp, 0, sizeof(hwtstamp));
	strncpy(hwtstamp.ifr_name, interface, sizeof(hwtstamp.ifr_name));
	hwtstamp.ifr_data = (void *)&hwconfig;
	memset(&hwconfig, 0, sizeof(hwconfig));

    if (st_tstamp_flags & SOF_TIMESTAMPING_TX_HARDWARE) {
		hwconfig.tx_type = HWTSTAMP_TX_ON;
	} else {
		hwconfig.tx_type = HWTSTAMP_TX_OFF;
	}
	
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

void get_timestamp(struct msghdr *msg, struct timespec **stamp, int recvmsg_flags)
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

				// if (recvmsg_flags & MSG_ERRQUEUE)
				// 	pkts->txcount_flag = 1;
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

void save_tstamp(struct timespec *stamp, unsigned char *data, size_t length, __s32 tx_seq, int recvmsg_flags)
{
    __u16 pkt_seq;
	int idx;
	int got_tx = 0;
	int got_rx = 0;

    if (tx_seq >= 0) {
		got_tx = 1;
		pkt_seq = tx_seq;
		// DEBUG("Got TX seq %d. %lu.%lu\n", pkt_seq, stamp->tv_sec, stamp->tv_nsec);
        // printf("Got TX seq %d. %lu.%lu\n", pkt_seq, stamp->tv_sec, stamp->tv_nsec);
		send_timestamp_arr[time_index].tv_sec = stamp->tv_sec;
		send_timestamp_arr[time_index].tv_nsec = stamp->tv_nsec;
		send_sequence_ids[time_index] = pkt_seq;
		time_index++;
	} else if (tx_seq == -1) {
        // printf("Got RX seq %d. %lu.%lu\n", atoi(data), stamp->tv_sec, stamp->tv_nsec);
		recv_timestamp_arr[time_index].tv_sec = stamp->tv_sec;
		recv_timestamp_arr[time_index].tv_nsec = stamp->tv_nsec;
		recv_sequence_ids[time_index] = atoi(data);
		time_index++;
    } else {
		// If the packet we read was not the tx packet then set back
		// txcount_flag and try again.
		// if (recvmsg_flags & MSG_ERRQUEUE)
		// 	pkts->txcount_flag = 0;
		return;
	}
}

static void recvpacket(int sock, int recvmsg_flags,
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
		get_timestamp(&msg, &stamp, recvmsg_flags);
		if (!stamp)
			return;
		save_tstamp(stamp, msg.msg_iov->iov_base,
			    res, tx_seq, recvmsg_flags);
	}
}

void rcv_xmit_tstamp(int sock, __u16 tx_seq) {
	fd_set readfs, errorfs;
	int res;

    FD_ZERO(&readfs);
    FD_ZERO(&errorfs);
    FD_SET(sock, &readfs);
    FD_SET(sock, &errorfs);

    // printf("1 IN rcv_xmit_tstamp\n");
    res = select(sock + 1, &readfs, 0, &errorfs, NULL);
    // printf("2 IN rcv_xmit_tstamp\n");
    if (res > 0) {
        // printf("3 IN rcv_xmit_tstamp\n");
        // recvpacket(sock, 0, pkts, tx_seq);
        recvpacket(sock, MSG_ERRQUEUE, tx_seq);
    }
}

int setup_sock(int sock)
{
    int so_tstamp_flags = 0;
    so_tstamp_flags |= (SOF_TIMESTAMPING_TX_HARDWARE | SOF_TIMESTAMPING_OPT_TSONLY);
    so_tstamp_flags |= (SOF_TIMESTAMPING_RX_HARDWARE | SOF_TIMESTAMPING_OPT_CMSG);
    so_tstamp_flags |= SOF_TIMESTAMPING_RAW_HARDWARE;

    struct ifreq device;
    memset(&device, 0, sizeof(device));
	strncpy(device.ifr_name, interface, sizeof(device.ifr_name));

    if (ioctl(sock, SIOCGIFINDEX, &device) < 0)
		bail("getting interface index");

    setup_hwconfig(interface, sock, so_tstamp_flags);

    if (so_tstamp_flags &&
	    setsockopt(sock, SOL_SOCKET, SO_TIMESTAMPING,
		       &so_tstamp_flags, sizeof(so_tstamp_flags)) < 0)
		printf("setsockopt SO_TIMESTAMPING not supported\n");

    int val;
    socklen_t len;
    /* verify socket options */
	len = sizeof(val);

    if (getsockopt(sock, SOL_SOCKET, SO_TIMESTAMPING, &val, &len) < 0) {
		printf("%s: %s\n", "getsockopt SO_TIMESTAMPING", strerror(errno));
	} else {
		// DEBUG("SO_TIMESTAMPING %d\n", val);
		if (val != so_tstamp_flags)
			printf("   not the expected value %d\n", so_tstamp_flags);
	}

    setsockopt_txtime(sock);

    return sock;

}


void *rcv_pkt(int sockfd)
{
    // struct rx_thread_data *data = arg;
	// int sock = data->sockfd;
    int sock = sockfd;
	fd_set readfs, errorfs;
	int res;

	while (running) {
		FD_ZERO(&readfs);
		FD_ZERO(&errorfs);
		FD_SET(sock, &readfs);
		FD_SET(sock, &errorfs);

		struct timeval tv = {0, 100000};   // sleep for ten minutes!
		res = select(sock + 1, &readfs, 0, &errorfs, &tv);
		/*res = select(sock + 1, &readfs, 0, &errorfs, NULL);*/
		if (res > 0) {
			recvpacket(sock, 0, -1);
		}
	}

}