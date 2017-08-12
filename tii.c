#include <sys/stat.h>

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

enum {
	NAMELEN = 128,
	MAXSRV = 16,
	MAXCHN = 32,
	CTRL_L = 0xC, /* TODO use '^L' */
	CTRL_H = 0x8,
	BCKSP = 0x7f
};

struct channel {
	char name[NAMELEN];
	int notify; /* TODO char? */
};

struct server {
	char name[NAMELEN];
	struct channel chs[MAXCHN];
	size_t i;
};

static void
die(char *msg)
{
	fputs(msg, stderr);
	exit(EXIT_FAILURE);
}

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
	char p[BUFSIZ] = {0}; /* TODO better safe than sorry? */
	/* TODO BUFSIZ is too big */

	/* TODO refactor and clean */
	/* TODO check bounds of n */

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
handle_input(char *buf, size_t n, size_t *i, size_t *chi)
{
	struct pollfd fds = {0};
	char c;

	/* TODO define a SINGLE responsibility for this function! */

	fds.fd = STDIN_FILENO;
	fds.events = POLLIN;
	if (!poll(&fds, 1, 10)) /* TODO less magical numbers */
		return;
	/* TODO check POLLIN */

	c = getchar();

	/* TODO ^D exit */
	/* TODO what if overflowing terminal width? */
	if (c == CTRL_L)
		(*chi)++; /* TODO check limit */

	if (c == CTRL_H)
		*chi ? (*chi)-- : 0; /* TODO prettier expression */

	if (c == CTRL_H || c == CTRL_L) {
		buf[0] = '\0';
		return;
		/* TODO better ctrl policy */
	}

	if (c == BCKSP) {
		(*i) ?  buf[--(*i)] = '\0' : 0; /* TODO less cryptic */
		/* TODO check all edge cases */
		return;
	}

	/* TODO check if buf is only blanks before sending */
	if (c == '\n' && !(*i)) /* TODO handle in send function? */
		return;

	if (*i >= n - 2) /* TODO better bounds */
		buf[n - 2] = c = '\n'; /* TODO too many assignments */
	else if (isprint(c) || isspace(c))
		buf[(*i)++] = c; /* TODO too complex expression */
}

static void
raw_term(void)
{
	struct termios attr;

	tcgetattr(STDIN_FILENO, &attr);
	attr.c_lflag &= ~(ICANON | ECHO);
	/* TODO sufficient with just setbuf */
	tcsetattr(STDIN_FILENO, TCSANOW, &attr);
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
	closedir(ds); /* TODO check error */
}

static void
print_out(struct channel *ch, size_t chi)
{
	char cmd[BUFSIZ] = {0}; /* TODO better to be safe than sorry? */

	strcat(cmd, "tail -n24 ./"); /* TODO -n argument */
	/* TODO use strncat or index */
	if (chi) {
		strcat(cmd, "/");
		strcat(cmd, ch[chi].name);
	}
	strcat(cmd, "/out");
	system(cmd);
	/* TODO use read() and select() */
}

static void
add_server(struct server *svs, size_t n, char *name)
{
	size_t i;

	for (i = 0; i < n; i++) {
		if (svs[i].name[0] == 0) {
			strncpy(svs[i].name, name, sizeof(svs[i].name));
			return;
		}

		if (!strncmp(svs[i].name, name, sizeof(svs[i].name)))
			return;
	}
}

static int
isdir(struct dirent *de)
{
	struct stat sb;

	stat(de->d_name, &sb);

	return S_ISDIR(sb.st_mode);
}

static void
find_servers(struct server *svs, size_t n)
{
	DIR *ds;
	struct dirent *de;

	ds = opendir(".");
	if (!ds)
		die("find_servers: failed to open irc directory");

	errno = 0;
	while ((de = readdir(ds))) {
		if (isdir(de) && de->d_name[0] != '.')
			add_server(svs, n, de->d_name);
	}
	if (errno)
		die("find_servers: error reading directory entries");
}

int
main(void)
{
	struct server servers[MAXSRV] = {0};
	struct channel ch[MAXCHN] = {0};
	char in[BUFSIZ] = {0};
	size_t chi = 0;
	time_t t = time(0);
	size_t i = 0; /* TODO single responsibility */

	find_servers(servers, MAXSRV);

	parse_dirs(ch);

	/* TODO refactor */
	raw_term();
	setbuf(stdout, 0); /* TODO less hacky sollution to print order */
	while (1) {
		if (time(0) - t) {
			print_out(ch, chi);
			print_channels(ch, chi);
			t = time(0);
		}

		fwrite("\r", sizeof(char), 1, stdout); /* TODO check return */
		printf("in: %s", in); /* TODO check return */
		handle_input(in, sizeof(in), &i, &chi);

		if (strchr(in, '\n')) { /* TODO responsibility */
			send_input(in, strlen(in), ch, chi);
			i = 0;
			memset(in, 0, BUFSIZ); /* TODO check return */
			/* TODO is memset necessary? */
		}
	}

	return EXIT_SUCCESS;
}
