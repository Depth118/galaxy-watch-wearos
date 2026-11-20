/*
 * wpcd: userspace keeper for the SM-R800's IDT P9222 wireless charging
 * receiver (i2c-3, addr 0x61, 16-bit register addresses).
 *
 * Our Android kernel has no P9222 driver. This replays what Samsung's Tizen
 * driver (p9222_charger.c) + sec_battery do while the watch is on the pad:
 *   attach : enable vbat monitoring (VRECT_SET=9, WPC_FLAG bit7),
 *            set AP battery mode (CC below cc_cv_threshold, CC/CV above),
 *            request the TX ID from the pad (3x)
 *   period : kick the Samsung watchdog (COMMAND bit7), re-assert AP mode,
 *            read any TX data packet, send CS100 once the battery is full
 * and logs the chip's state so drop-outs can be diagnosed.
 *
 *   wpcd r <reg>            read one register (hex)
 *   wpcd w <reg> <val>      write one register (hex)
 *   wpcd dump               print the status registers once
 *   wpcd kick               set COMMAND_REG (0x4E) bit 7 once
 *   wpcd loop <seconds>     keep the pad session alive forever
 */
#include <errno.h>
#include <stdarg.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <linux/netlink.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>

#define BUS	"/dev/i2c-3"
#define ADDR	0x61

#define REG_INT_STATUS_L	0x34
#define REG_INT_STATUS_H	0x35
#define REG_INT_L		0x36
#define REG_INT_H		0x37
#define REG_CHG_STATUS		0x3A
#define REG_EPT			0x3B
#define REG_VOUT_L		0x3C
#define REG_VRECT_SET		0x3F
#define REG_VRECT_L		0x40
#define REG_IOUT_L		0x44
#define REG_DIE_TEMP_L		0x46
#define REG_OP_FREQ_L		0x48
#define REG_SYS_OP_MODE		0x4D
#define REG_COMMAND		0x4E
#define REG_PACKET_HEADER	0x50
#define REG_RX_DATA_CMD		0x51
#define REG_RX_DATA_VAL0	0x52
#define REG_INT_CLEAR_L		0x56
#define REG_TX_DATA_CMD		0x58
#define REG_TX_DATA_VAL0	0x59
#define REG_WPC_FLAG		0x80
#define REG_AP_BATT_MODE	0x9A

#define CMD_SS_WDT		0x80
#define CMD_SEND_CHG_STS	0x10
#define CMD_SEND_RX_DATA	0x01
#define FLAG_VBAT_MONITOR	0x80
#define INT_TX_DATA_RECEIVED	0x10

#define AP_CC_CV		0x06
#define AP_INCOMPAT_CC_CV	0x07
#define AP_CC			0x10
#define AP_INCOMPAT_CC		0x11

#define CC_CV_THRESHOLD		86	/* DT p9222,cc_cv_threshold */

/*
 * Foreign-object detection calibration (DT battery,fod_data, written by
 * Tizen's p9222_fod_set() on every attach, to FOD_0A..). Without it the
 * receiver reports uncalibrated received power, the pad sees a power loss it
 * blames on a foreign object, and it stops after 0.5-3 min with its LED
 * blinking red (2026-11-20: sessions of 5-180 s, then no re-attach).
 */
#define REG_FOD_0A		0x68
static const unsigned char fod_data[] = {
	0xcc, 0x09, 0x96, 0x1c, 0x82, 0x37, 0x82, 0x39, 0x9d, 0x05, 0x9d, 0x06 };
static const unsigned char compat_tx[] = { 0x54, 0x70, 0x71, 0x72, 0x73 };

static int fd;

static void logf_(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void logf_(const char *fmt, ...)
{
	char ts[32];
	time_t t = time(0);
	va_list ap;
	strftime(ts, sizeof(ts), "%m-%d %H:%M:%S", localtime(&t));
	printf("%s ", ts);
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	putchar('\n');
	fflush(stdout);
}

static int rd(unsigned reg, unsigned char *val)
{
	unsigned char w[2] = { reg >> 8, reg & 0xff };
	struct i2c_msg m[2] = {
		{ .addr = ADDR, .flags = 0, .len = 2, .buf = w },
		{ .addr = ADDR, .flags = I2C_M_RD, .len = 1, .buf = val },
	};
	struct i2c_rdwr_ioctl_data d = { .msgs = m, .nmsgs = 2 };
	return ioctl(fd, I2C_RDWR, &d) == 2 ? 0 : -1;
}

static int wr(unsigned reg, unsigned char val)
{
	unsigned char w[3] = { reg >> 8, reg & 0xff, val };
	struct i2c_msg m = { .addr = ADDR, .flags = 0, .len = 3, .buf = w };
	struct i2c_rdwr_ioctl_data d = { .msgs = &m, .nmsgs = 1 };
	return ioctl(fd, I2C_RDWR, &d) == 1 ? 0 : -1;
}

static int upd(unsigned reg, unsigned char val, unsigned char mask)
{
	unsigned char v;
	if (rd(reg, &v))
		return -1;
	return wr(reg, (v & ~mask) | (val & mask));
}

static int rd16(unsigned reg)
{
	unsigned char l, h;
	if (rd(reg, &l) || rd(reg + 1, &h))
		return -1;
	return l | h << 8;
}

static int kick(void)
{
	return upd(REG_COMMAND, CMD_SS_WDT, CMD_SS_WDT);
}

static int sysfs_int(const char *path, int def)
{
	char buf[32];
	int n, f = open(path, O_RDONLY);
	if (f < 0)
		return def;
	n = read(f, buf, sizeof(buf) - 1);
	close(f);
	if (n <= 0)
		return def;
	buf[n] = 0;
	return atoi(buf);
}

static int battery_full(void)
{
	char buf[32] = "";
	int n, f = open("/sys/class/power_supply/battery/status", O_RDONLY);
	if (f < 0)
		return 0;
	n = read(f, buf, sizeof(buf) - 1);
	close(f);
	return n > 0 && !strncmp(buf, "Full", 4);
}

/* p9222_write_ap_mode: write + read back, up to 3 tries */
static int write_ap_mode(unsigned char mode)
{
	unsigned char v = 0xff;
	int i;
	for (i = 0; i < 3; i++) {
		wr(REG_AP_BATT_MODE, mode);
		if (!rd(REG_AP_BATT_MODE, &v) && v == mode)
			return 0;
		usleep(100000);
	}
	logf_("ap_mode 0x%02x not taken (reads 0x%02x)", mode, v);
	return -1;
}

/* p9222_wpc_request_tx_id(charger, 3): the TX ID request alone */
static void request_tx_id_only(void)
{
	int i;
	for (i = 0; i < 3; i++) {
		wr(REG_PACKET_HEADER, 0x28);
		wr(REG_RX_DATA_CMD, 0x01);
		wr(REG_RX_DATA_VAL0, 0x00);
		wr(REG_COMMAND, CMD_SEND_RX_DATA);
		usleep(300000);
	}
}

static void request_tx_id(void)
{
	int i;
	for (i = 0; i < 3; i++) {
		wr(REG_PACKET_HEADER, 0x28);
		wr(REG_RX_DATA_CMD, 0x01);
		wr(REG_RX_DATA_VAL0, 0x00);
		wr(REG_COMMAND, CMD_SEND_RX_DATA);
		usleep(300000);
	}
	/*
	 * p9222_wpc_send_rx_id: then tell the pad which watch this is (RX-ID
	 * 0x54 from the stock DT, p9222,rx_id), 3x. Without it a Samsung watch
	 * pad charges for ~180 s, then blinks red and stops. (Repo commit
	 * 20ec078; the 11-18 rebuild came from a tree without it.)
	 */
	for (i = 0; i < 3; i++) {
		wr(REG_PACKET_HEADER, 0x28);	/* P9222_HEADER_AFC_CONF */
		wr(REG_RX_DATA_CMD, 0x0e);	/* P9222_RX_DATA_COM_RX_ID */
		wr(REG_RX_DATA_VAL0, 0x54);
		wr(REG_COMMAND, CMD_SEND_RX_DATA);
		usleep(100000);
	}
	logf_("rx id 0x54 sent");
}

/* p9222_send_cs100: tell the pad the battery is at 100% */
static void send_cs100(void)
{
	int i, j;
	unsigned char v;
	for (i = 0; i < 2; i++) {
		if (wr(REG_CHG_STATUS, 100))
			return;
		for (j = 0; j < 4; j++) {	/* p9222_set_cmd_reg */
			upd(REG_COMMAND, CMD_SEND_CHG_STS, CMD_SEND_CHG_STS);
			usleep(250000);
			if (rd(REG_COMMAND, &v) || !(v & CMD_SEND_CHG_STS))
				break;
		}
		usleep(250000);
	}
}

/*
 * Hold a kernel wakelock while on the pad: if the board suspended mid-session
 * the kicks would stop and the pad would end power transfer.
 */
static void wakelock(int hold)
{
	static int held;
	const char *path = hold ? "/sys/power/wake_lock" : "/sys/power/wake_unlock";
	int f;
	if (hold == held)
		return;
	f = open(path, O_WRONLY);
	if (f < 0 || write(f, "wpcd", 4) != 4)
		logf_("wakelock %s failed (%s)", hold ? "hold" : "release", strerror(errno));
	else
		held = hold;
	if (f >= 0)
		close(f);
}

static double now_s(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec + t.tv_nsec / 1e9;
}

/* one line of chip state, shared by dump() and the flight recorder */
static void snapshot(char *out, size_t len)
{
	unsigned char op = 0, ap = 0, flag = 0, cmd = 0, ept = 0, sl = 0, sh = 0, il = 0, ih = 0;
	rd(REG_SYS_OP_MODE, &op);
	rd(REG_AP_BATT_MODE, &ap);
	rd(REG_WPC_FLAG, &flag);
	rd(REG_COMMAND, &cmd);
	rd(REG_EPT, &ept);
	rd(REG_INT_STATUS_L, &sl);
	rd(REG_INT_STATUS_H, &sh);
	rd(REG_INT_L, &il);
	rd(REG_INT_H, &ih);
	snprintf(out, len, "vrect=%d vout=%d iout=%d freq=%d temp=%d op=%02x ap=%02x flag=%02x cmd=%02x ept=%02x stat=%02x%02x int=%02x%02x soc=%d",
		 rd16(REG_VRECT_L), rd16(REG_VOUT_L), rd16(REG_IOUT_L),
		 rd16(REG_OP_FREQ_L), rd16(REG_DIE_TEMP_L) & 0xfff, op, ap, flag, cmd, ept,
		 sh, sl, ih, il, sysfs_int("/sys/class/power_supply/battery/capacity", -1));
}

static void dump(const char *why)
{
	char buf[256];
	snapshot(buf, sizeof(buf));
	logf_("%s %s", why, buf);
}

/*
 * Flight recorder: the last HIST ticks of chip state (one per period),
 * written to the log only when the pad session drops, so the minute before
 * a drop-out is visible without logging every tick.
 */
#define HIST 12
static struct {
	char ts[16];
	double gap;
	char line[256];
} hist[HIST];
static int hist_n;

static void hist_add(double gap)
{
	int i = hist_n++ % HIST;
	time_t t = time(0);
	strftime(hist[i].ts, sizeof(hist[i].ts), "%H:%M:%S", localtime(&t));
	hist[i].gap = gap;
	snapshot(hist[i].line, sizeof(hist[i].line));
}

static void hist_flush(void)
{
	int i, n = hist_n < HIST ? hist_n : HIST;
	if (!n)
		return;
	logf_("last %d ticks before the drop (gap = s since previous kick):", n);
	for (i = hist_n - n; i < hist_n; i++)
		logf_("  %s gap=%.1f %s", hist[i % HIST].ts, hist[i % HIST].gap, hist[i % HIST].line);
	hist_n = 0;
}

struct session {
	int compatible;		/* -1 unknown, 0 no, 1 yes */
	int tx_id;
	int cv;
	int cs100_sent;
	int fod_done;		/* FOD table written this session */
	int ticks;
	double last_kick;	/* monotonic time of the last accepted kick */
};

static unsigned char want_ap_mode(struct session *s, int soc)
{
	/* once CV was entered stay there until detach (cc_cv_charge_select) */
	if (soc >= CC_CV_THRESHOLD)
		s->cv = 1;
	if (s->compatible == 0)
		return s->cv ? AP_INCOMPAT_CC_CV : AP_INCOMPAT_CC;
	return s->cv ? AP_CC_CV : AP_CC;
}

static void check_tx_data(struct session *s)
{
	unsigned char il = 0, cmd = 0, val = 0;
	size_t i;
	if (rd(REG_INT_L, &il) || !(il & INT_TX_DATA_RECEIVED))
		return;
	rd(REG_TX_DATA_CMD, &cmd);
	rd(REG_TX_DATA_VAL0, &val);
	/* clear the interrupt: write the clear mask, then latch it with COMMAND bit5 */
	wr(REG_INT_CLEAR_L, INT_TX_DATA_RECEIVED);
	upd(REG_COMMAND, 0x20, 0x20);
	if (cmd == 0x01) {
		int was = s->compatible, old_id = s->tx_id;
		s->tx_id = val;
		s->compatible = 0;
		for (i = 0; i < sizeof(compat_tx); i++)
			if (compat_tx[i] == val)
				s->compatible = 1;
		if (was != s->compatible || old_id != val)
			logf_("tx id 0x%02x (%s)", val, s->compatible ? "compatible" : "incompatible");
	} else if (cmd == 0x19) {
		s->compatible = 0;
		logf_("tx says incompatible");
	} else {
		logf_("tx data cmd=0x%02x val=0x%02x", cmd, val);
	}
}

/* p9222_fod_set: once per session, as soon as the receiver answers */
static void fod_set(struct session *s)
{
	int i, bad = 0;
	unsigned char v;

	for (i = 0; i < (int)sizeof(fod_data); i++)
		if (wr(REG_FOD_0A + i, fod_data[i]) < 0 ||
		    rd(REG_FOD_0A + i, &v) < 0 || v != fod_data[i])
			bad++;
	s->fod_done = bad == 0;
	logf_("fod table %s (%d of %d mismatched)", bad ? "NOT written" : "written",
	      bad, (int)sizeof(fod_data));
}

/* everything Tizen does once power transfer runs (also after a receiver reset) */
static void setup_session(struct session *s, int soc)
{
	fod_set(s);
	/* p9222_enable_vbat_monitoring */
	wr(REG_VRECT_SET, 9);
	upd(REG_WPC_FLAG, FLAG_VBAT_MONITOR, FLAG_VBAT_MONITOR);
	if (kick() == 0)
		s->last_kick = now_s();
	/* WPC_MODE_ATTACH */
	write_ap_mode(want_ap_mode(s, soc));
	request_tx_id();
	check_tx_data(s);
	dump("setup");
}

static void on_attach(struct session *s)
{
	int soc = sysfs_int("/sys/class/power_supply/battery/capacity", 50);
	int i, iout = -1, vout = -1;
	memset(s, 0, sizeof(*s));
	s->compatible = -1;
	usleep(500000);
	dump("attach");
	/*
	 * Wait for power transfer before touching the receiver. The pad drops
	 * sessions (blinking red LED) when we configure it during negotiation:
	 * failed attaches showed vrect 5.0-5.7 V with 0 mA and the chip gone a
	 * second later, good ones already drew ~200 mA at 4.3 V.
	 */
	for (i = 0; i < 40; i++) {
		iout = rd16(REG_IOUT_L);
		vout = rd16(REG_VOUT_L);
		if (iout < 0 || vout < 0) {
			logf_("receiver gone during negotiation");
			return;
		}
		if (iout > 20 && vout > 3500 && vout < 4900)
			break;
		usleep(150000);
	}
	logf_("power transfer %s after %d ms (vout %d iout %d)",
	      i < 40 ? "up" : "not seen", i * 150, vout, iout);
	setup_session(s, soc);
}

static void on_tick(struct session *s, int period)
{
	int soc = sysfs_int("/sys/class/power_supply/battery/capacity", 50);
	unsigned char ap = 0, want;
	double t = now_s(), gap = s->last_kick ? t - s->last_kick : 0;

	if (kick() == 0) {
		if (gap > period + 2)
			logf_("late kick: %.1f s since the previous one (period %d s)", gap, period);
		s->last_kick = t;
	} else {
		logf_("kick failed (%.1f s since the last accepted one)", gap);
	}
	hist_add(gap);
	/* the attach can give up before the receiver answers ("receiver gone
	 * during negotiation") while the session then comes up anyway */
	if (!s->fod_done && rd16(REG_IOUT_L) > 20)
		fod_set(s);
	check_tx_data(s);
	if (s->compatible < 0 && s->ticks < 5)
		request_tx_id();
	/*
	 * Tizen re-requests the TX ID for the whole session: its auth check
	 * runs 25 s after attach and after every TX data reply
	 * (WPC_AUTH_DELAY_TIME), so the pad hears from the watch every ~25 s.
	 * Kept for parity; it did not change the 2026-11-20 drops, which were
	 * the pad losing power (a latched pad error, then a power strip glitch).
	 */
	else if (s->ticks > 0 && (s->ticks * period) % 25 < period)
		request_tx_id_only();
	/*
	 * AP battery mode back at its power-on default (0x00): the receiver
	 * reset under us (a short power dip on the pad side, 2026-11-20), and
	 * lost the FOD table and vbat monitoring with it. Redo the whole setup.
	 */
	if (rd(REG_AP_BATT_MODE, &ap) == 0 && ap == 0x00) {
		logf_("receiver reset during the session: setting it up again");
		s->fod_done = 0;
		s->compatible = -1;
		setup_session(s, soc);
		s->ticks++;
		return;
	}
	/* WPC_MODE_NORMAL: re-assert when it changed under us */
	want = want_ap_mode(s, soc);
	if (rd(REG_AP_BATT_MODE, &ap) == 0 && ap != want) {
		logf_("ap_mode drifted 0x%02x -> 0x%02x", ap, want);
		write_ap_mode(want);
	}
	if (!s->cs100_sent && battery_full()) {
		logf_("battery full: sending CS100");
		send_cs100();
		s->cs100_sent = 1;
	}
	if (s->ticks * period % 60 == 0)
		dump("mon");
	s->ticks++;
}


/*
 * Off the pad, block on kernel uevents instead of probing i2c every second:
 * charger attach (muic acok) makes the power_supply class emit a uevent.
 * Returns 1 when a power_supply uevent arrived, 0 on timeout.
 */
static int uev_fd = -1;

static void uevent_open(void)
{
	struct sockaddr_nl sa = { .nl_family = AF_NETLINK, .nl_groups = 1 };
	uev_fd = socket(AF_NETLINK, SOCK_DGRAM | SOCK_CLOEXEC, NETLINK_KOBJECT_UEVENT);
	if (uev_fd >= 0 && bind(uev_fd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
		close(uev_fd);
		uev_fd = -1;
	}
	if (uev_fd < 0)
		logf_("uevent socket failed (%s), falling back to 1 s polling", strerror(errno));
}

static int wait_power_uevent(int timeout_ms)
{
	struct pollfd p = { .fd = uev_fd, .events = POLLIN };
	char buf[2048];
	int hit = 0;
	if (uev_fd < 0) {
		sleep(1);
		return 1;
	}
	if (poll(&p, 1, timeout_ms) <= 0)
		return 0;
	/*
	 * Drain everything queued. The battery driver reports every 10 s; a
	 * battery report that still says Discharging means nothing changed.
	 * Anything else from power_supply (charging, full, a charger going
	 * online) may be the pad, so probe the receiver.
	 */
	for (;;) {
		ssize_t i, n = recv(uev_fd, buf, sizeof(buf) - 1, MSG_DONTWAIT);
		if (n <= 0)
			break;
		for (i = 0; i < n; i++)	/* NUL-separated KEY=VALUE pairs */
			if (!buf[i])
				buf[i] = '\n';
		buf[n] = 0;
		if (strstr(buf, "SUBSYSTEM=power_supply") &&
		    !(strstr(buf, "POWER_SUPPLY_NAME=battery\n") &&
		      strstr(buf, "POWER_SUPPLY_STATUS=Discharging\n")))
			hit = 1;
	}
	return hit;
}

int main(int argc, char **argv)
{
	if (argc < 2)
		goto usage;
	fd = open(BUS, O_RDWR);
	if (fd < 0) {
		perror(BUS);
		return 1;
	}
	if (!strcmp(argv[1], "r") && argc == 3) {
		unsigned char v;
		if (rd(strtoul(argv[2], 0, 16), &v)) {
			perror("read");
			return 1;
		}
		printf("0x%02x\n", v);
		return 0;
	}
	if (!strcmp(argv[1], "w") && argc == 4) {
		if (wr(strtoul(argv[2], 0, 16), strtoul(argv[3], 0, 16))) {
			perror("write");
			return 1;
		}
		return 0;
	}
	if (!strcmp(argv[1], "dump")) {
		dump("dump");
		return 0;
	}
	if (!strcmp(argv[1], "uev") && argc == 3) {	/* print uevents for N s */
		time_t end = time(0) + atoi(argv[2]);
		char buf[2048];
		uevent_open();
		while (uev_fd >= 0 && time(0) < end) {
			struct pollfd p = { .fd = uev_fd, .events = POLLIN };
			ssize_t i, n;
			if (poll(&p, 1, 1000) <= 0)
				continue;
			n = recv(uev_fd, buf, sizeof(buf) - 1, 0);
			for (i = 0; i < n; i++)
				if (!buf[i])
					buf[i] = ' ';
			buf[n > 0 ? n : 0] = 0;
			logf_("%s", buf);
		}
		return 0;
	}
	if (!strcmp(argv[1], "kick")) {
		if (kick()) {
			perror("kick");
			return 1;
		}
		return 0;
	}
	if (!strcmp(argv[1], "loop") && argc == 3) {
		struct session s;
		int period = atoi(argv[2]), on_pad = 0, misses = 0;
		unsigned char v;
		if (period < 1)
			period = 5;
		int burst = 0;	/* seconds of fast probing left after a uevent */
		logf_("wpcd started, period %ds (off pad: uevent wait)", period);
		/*
		 * The pad drops the session when kicks stop. Keep this process
		 * responsive under load: higher priority, no swapped-out pages,
		 * never chosen by the low-memory killer.
		 */
		if (setpriority(PRIO_PROCESS, 0, -10))
			logf_("setpriority failed: %s", strerror(errno));
		if (mlockall(MCL_CURRENT | MCL_FUTURE))
			logf_("mlockall failed: %s", strerror(errno));
		{
			int f = open("/proc/self/oom_score_adj", O_WRONLY);
			if (f >= 0) {
				if (write(f, "-1000", 5) != 5)
					logf_("oom_score_adj write failed");
				close(f);
			}
		}
		uevent_open();
		for (;;) {
			/* the chip is only powered (and answering) while on the pad */
			int up = rd(REG_SYS_OP_MODE, &v) == 0;
			if (up && !on_pad) {
				on_pad = 1;
				misses = 0;
				logf_("on pad");
				wakelock(1);
				on_attach(&s);
			} else if (up) {
				misses = 0;
				on_tick(&s, period);
			} else if (on_pad && ++misses >= 2) {
				on_pad = 0;
				logf_("off pad (after %d s)", s.ticks * period);
				hist_flush();
				wakelock(0);
			}
			if (up || on_pad) {
				sleep(up ? period : 1);
			} else if (burst > 0) {
				/* the receiver needs a moment to power up after attach */
				burst--;
				sleep(1);
			} else if (wait_power_uevent(60000)) {
				burst = 8;
			}
		}
	}
usage:
	fprintf(stderr, "usage: wpcd r <reg> | w <reg> <val> | dump | kick | loop <sec>\n");
	return 2;
}
