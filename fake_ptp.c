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

int main(int argc, char **argv)
{
    int tx_sock;
    unsigned char mac[ETH_ALEN];

    Packets pkts;
    memset(&pkts, 0, sizeof(Packets));

    pkts.txcount_flag = false;
	pkts.list_head = 0;
	pkts.next_seq = 0;
    pkts.frame = sync_packet;
	pkts.frame_size = sizeof(sync_packet);

    pthread_mutex_init(&pkts.list_lock, NULL);

    pkts.list_len = 65536;
    pkts.list = calloc(sizeof(struct pkt_time), pkts.list_len);
	if (!pkts.list)
		return ENOMEM;

    /* Sender */
    tx_sock = setup_tx_sock("enp65s0f0np0", 0, true, false, false);
    if (tx_sock < 0) {
		ERR("failed setting up TX socket\n");
		// goto out_err_tx_sock;
	}

    get_smac(tx_sock, "enp65s0f0np0", mac);
	set_smac(pkts.frame, mac);

// out_err_tx_sock:
	// pthread_kill(rx_thread, SIGINT);
	// close(rx_args.sockfd);
	// return err;

    free(pkts.list);
}



