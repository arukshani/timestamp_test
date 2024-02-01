




#ifndef CLOCK_INVALID
#define CLOCK_INVALID -1
#endif

#define CLOCKFD 3
#define FD_TO_CLOCKID(fd)	((clockid_t) ((((unsigned int) ~fd) << 3) | CLOCKFD))
#define CLOCKID_TO_FD(clk)	((unsigned int) ~((clk) >> 3))


clockid_t clkid;
static clockid_t get_nic_clock_id(void)
{
	int fd;
    char *device = DEVICE;
    clockid_t clkid;

    fd = open(device, O_RDWR);
	if (fd < 0) {
		fprintf(stderr, "opening %s: %s\n", device, strerror(errno));
		return -1;
	}

	clkid = FD_TO_CLOCKID(fd);

	if (CLOCK_INVALID == clkid) {
		fprintf(stderr, "failed to read clock id\n");
		return -1;
	}
	return clkid;
}

static struct timespec get_nicclock(void)
{
	struct timespec ts;
	clock_gettime(clkid, &ts);
	return ts;
}

static struct timespec get_monoclock(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts;
}

// struct thread_data {
// 	struct timespec ts;
// };

struct timespec now;
static int quit;

static void read_time()
{
	for ( ; !quit; ) 
	{
		now = get_nicclock();
	}
}



