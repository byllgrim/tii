#include <sys/stat.h>

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
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
	/* TODO ^J ^K ^U */
	BCKSP = 0x7f,
	ROWS = 24,
	COLS = 80
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
print_channels(struct server *srv)
{
	size_t i;

	/* TODO don't print when theres no change */

	printf("ch: ");
	for (i = 0; i < MAXCHN && srv->chs[i].name[0]; i++) {
		if (i == srv->i)
			printf("[%s]", srv->name);
		else
			printf(" %s%c",
			       srv->chs[i].name,
			       srv->chs[i].notify ? '*' : ' ');
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
print_tail(int fd)
{
	off_t pos;
	char buf[BUFSIZ];
	size_t n;

	pos = lseek(fd, 0, SEEK_END);

	pos -= ROWS*COLS;
	if (pos < 0)
		pos = 0;
	lseek(fd, pos, SEEK_SET);

	for (; (n = read(fd, buf, sizeof(buf))); )
		write(STDOUT_FILENO, buf, n);
}

static void
print_out(struct server *srv)
{
	char cwd[BUFSIZ];
	int out;

	if (!getcwd(cwd, sizeof(cwd)))
		die("print_out: failed to get cwd\n");

	errno = 0;
	chdir(srv->name);
	chdir(srv->chs[srv->i].name);
	if (errno) /* TODO not all under same comb? */
		die("print_out: failed to cd into channel\n");

	out = open("out", O_RDONLY); /* TODO mode */
	if (out < 0)
		die("print_out: failed to open 'out' file\n");

	print_tail(out);

	close(out);
	chdir(cwd);
}

static void
init_server(struct server *srv, char *name)
{
	strncpy(srv->name, name, sizeof(srv->name));
	srv->chs[0].name[0] = '.';
	srv->i = 0;
}

static void
add_server(struct server *svs, size_t n, char *name)
{
	size_t i;

	for (i = 0; i < n; i++) {
		if (svs[i].name[0] == 0) {
			printf("found server %s\n", name);
			init_server(&svs[i], name);
			return;
		}

		if (!strncmp(svs[i].name, name, sizeof(svs[i].name)))
			return;
	}
}

static int
is_dir(struct dirent *de)
{
	struct stat sb;

	if (stat(de->d_name, &sb) < 0)
		die("is_dir: error on stat()\n");

	return S_ISDIR(sb.st_mode);
}

static void
find_servers(struct server *svs, size_t n)
{
	DIR *ds;
	struct dirent *de;

	printf("scanning for irc servers\n");

	ds = opendir(".");
	if (!ds)
		die("find_servers: failed to open irc directory\n");

	errno = 0;
	for (; (de = readdir(ds)); ) {
		if (is_dir(de) && de->d_name[0] != '.')
			add_server(svs, n, de->d_name);
	}
	if (errno)
		die("find_servers: error reading directory entries\n");

	closedir(ds);
}

static void
add_channel(struct server *srv, char *name)
{
	size_t i;
	struct channel *chs = srv->chs;

	for (i = 0; i < sizeof(srv->chs); i++) {
		if (chs[i].name[0] == 0) {
			strncpy(chs[i].name, name, sizeof(chs[i].name));
			printf("adding %s to %s\n", name, srv->name);
			return;
		}

		if (!strncmp(chs[i].name, name, sizeof(chs[i].name)))
			return;
	}
}

static void
find_channels(struct server *srv)
{
	DIR *ds;
	struct dirent *de;

	printf("scanning for channels in %s\n", srv->name);

	ds = opendir(srv->name);
	if (!ds)
		die("find_channels: failed to open server directory\n");

	if (chdir(srv->name) < 0)
		die("find_channels: can't chdir\n");

	/* TODO dry from find_servers */
	errno = 0;
	for (; (de = readdir(ds)); ) {
		if (is_dir(de) && de->d_name[0] != '.')
			add_channel(srv, de->d_name);
	}
	if (errno)
		die("find_channels: error reading directory entries\n");

	chdir(".."); /* TODO getcwd() */
	closedir(ds);
}

int
main(void)
{
	struct server servers[MAXSRV] = {0};
	struct channel ch[MAXCHN] = {0};
	char in[BUFSIZ] = {0};
	size_t chi = 0;
	time_t t = time(0);
	size_t i;
	size_t cursrv = 0;

	find_servers(servers, MAXSRV);
	for (i = 0; i < MAXSRV && servers[i].name[0]; i++)
		find_channels(&servers[i]);

	/* TODO refactor */
	raw_term();
	setbuf(stdout, 0); /* TODO less hacky sollution to print order */
	i = 0;
	for (;;) {
		if (time(0) - t) {
			print_out(&servers[cursrv]);
			print_channels(&servers[cursrv]);
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
