#include <sys/stat.h>

#include <ctype.h>
#include <dirent.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

/* TODO unecessary includes */

enum {
	NAMELEN = 128,
	MAXCHN = 16,
	CTRL_L = 0xC, /* TODO use '^L' */
	CTRL_H = 0x8
};

struct channel {
	char name[NAMELEN];
	int notify;
};

static void
print_channels(struct channel *ch, size_t chi) /* TODO assuming MAXCH */
{
	/* TODO possible to take array of known size? */
	size_t i;

	printf("ch: ");
	for (i = 0; i < MAXCHN; i++) {
		if (i == chi)
			printf("[%s]", ch[i].name);
		else
			printf(" %s%c",
			       ch[i].name,
			       ch[i].notify ? '*' : ' ');
	}
	/* TODO refresh output immediately */
	putchar('\n');
}

static void
send_input(char *msg, size_t n, struct channel *ch, size_t chi)
{
	FILE *f;
	char p[BUFSIZ]; /* TODO too big size */

	/* TODO refactor and clean */
	/* TODO check bounds of n */

	p[0] = '\0';
	strcat(p, ".");
	if (chi) {
		strcat(p, "/");
		strcat(p, ch[chi].name);
	}
	strcat(p, "/in");
	/* TODO check if joined channel */
	/* TODO check if ii is running */
	if (!(f = fopen(p, "a"))) {
		printf("send_input: failed to open file\n");
		/* TODO proper error checking */
		return;
	}

	/* TODO proper error checking */
	/* TODO check if 'in' fifo points to anything */
	if (fwrite(msg, sizeof(char), n, f))
		printf("sent success\n");
	else
		printf("boo\n");

	fclose(f);
}

static void
get_input(char *buf, size_t n, size_t *i, size_t *chi)
{
	struct pollfd fds;
	char c;

	fds.fd = 0; /* TODO named stdin */
	fds.events = POLLIN;
	fds.revents = 0;
	if (!poll(&fds, 1, 10)) /* TODO less magical numbers */
		return;
	/* TODO check POLLIN */

	/* TODO don't wait if no input */
	/* TODO loop many times; don't hang */
	c = getchar(); /* TODO don't block output */

	/* TODO ^D exit */
	/* TODO what if overflowing terminal width? */
	if (c == CTRL_L)
		(*chi)++; /* TODO check limit */

	if (c == CTRL_H)
		*chi ? (*chi)-- : 0; /* TODO prettier */

	if (c == CTRL_H || c == CTRL_L) {
		buf[0] = '\0';
		return;
		/* TODO better ctrl policy */
	}

	if (*i >= n - 2) /* TODO better bounds */
		buf[n - 2] = c = '\n'; /* TODO too many assignments */
	else if (isprint(c) || isspace(c))
		buf[(*i)++] = c; /* TODO too complex expression */
}

static void
raw_term(void)
{
	struct termios attr;

	tcgetattr(0, &attr); /* TODO no magic numbers */
	attr.c_lflag &= ~(ICANON | ECHO);
	/* TODO sufficient with just setbuf */
	tcsetattr(0, TCSANOW, &attr);
}

static void
parse_dirs(struct channel *ch) /* TODO rename */
{
	DIR *ds;
	size_t i;
	struct dirent *de;
	struct stat sb;

	ds = opendir("./"); /* TODO error checking */
	ch[0].name[0] = '\0';
	for (i = 0; (de = readdir(ds)); ) {
		stat(de->d_name, &sb);
		if (!S_ISDIR(sb.st_mode) || de->d_name[0] == '.')
			continue;

		if (!ch[0].name[0]) {
			strncpy(ch[0].name, de->d_name, NAMELEN);
			/* TODO null termination */
			ds = opendir(ch[0].name); /* TODO error checking */
			chdir(ch[0].name); /* TODO no side-effects */
		} else {
			strncpy(ch[i].name, de->d_name, NAMELEN);
			/* TODO null termination */
			/* TODO duplication */
		}

		/* TODO handle DT_UNKNOWN */

		i++;
		/* TODO check if connected to channel */
	}
	/* TODO close dir */
}

static void
print_out(struct channel *ch, size_t chi)
{
	char cmd[BUFSIZ];

	cmd[0] = '\0';
	strcat(cmd, "tail -n24 ./"); /* TODO -n = ? */
	if (chi) {
		strcat(cmd, "/");
		strcat(cmd, ch[chi].name);
	}
	strcat(cmd, "/out");
	system(cmd);
	/* TODO use read() */
}

int
main(void)
{
	struct channel ch[MAXCHN] = {0};
	char in[BUFSIZ] = {0};
	size_t chi = 0;
	time_t t = 0;
	size_t i = 0; /* TODO single responsibility */

	parse_dirs(ch);
	chi = 0;

	/* TODO refactor */
	raw_term();
	t = time(0);
	setbuf(stdout, 0); /* TODO less hacky sollution to print order */
	while (1) {
		if (time(0) - t) {
			print_out(ch, chi);
			print_channels(ch, chi);
			t = time(0);
		}

		fwrite("\r", sizeof(char), 1, stdout); /* TODO check return */
		printf("in: %s", in); /* TODO check return */
		get_input(in, sizeof(in), &i, &chi);

		if (strchr(in, '\n')) { /* TODO responsibility */
			send_input(in, strlen(in), ch, chi);
			i = 0;
			memset(in, 0, BUFSIZ); /* TODO check return */
			/* TODO is memset necessary? */
		}

		/* TODO input don't block output */
	}

	return 0;
}
