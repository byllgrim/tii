#include <sys/stat.h>

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h> /* TODO remove */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

enum {
	NAMELEN = 128,
	MAXSRV = 16,
	MAXCHN = 32,
	CTRL_L = 0xC, /* TODO use '^L' */
	CTRL_H = 0x8,
	/* TODO ^J ^K ^U ^D */
	BCKSP = 0x7f,
	ROWS = 24,
	COLS = 80
};

struct inbuf {
	char txt[BUFSIZ];
	size_t i;
};

struct channel {
	char name[NAMELEN];
	int notify; /* TODO char? */
	int out;
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
	char *name;

	printf("ch: ");
	for (i = 0; i < MAXCHN && srv->chs[i].name[0]; i++) {
		name = i ? srv->chs[i].name : srv->name;
		if (i == srv->i)
			printf("[%s]", name);
		else
			printf(" %s%c",
			       name,
			       srv->chs[i].notify ? '*' : ' ');
	}
	putchar('\n');
}

static void
send_input(struct server *srv, struct inbuf *in)
{
	char cwd[BUFSIZ];
	int fd;
	ssize_t n;

	getcwd(cwd, sizeof(cwd)); /* TODO check return */
	chdir(srv->name); /* TODO check return */
	chdir(srv->chs[srv->i].name); /* TODO check return */
	
	/* TODO check if buf is only blanks before sending? */

	fd = open("in", O_WRONLY | O_NONBLOCK);
		/* TODO keep open all the time? */
		/* TODO don't create */
		/* TODO die if not existing/connected */
		/* TODO O_APPEND? */
	if (fd < 0) {
		if (errno == ENXIO) /* no fifo reader */
			goto exit;
		else
			die("send_input: cannot open 'in' file\n");
	}

	n = write(fd, in->txt, sizeof(in->txt));
	if (n != sizeof(in->txt))
		die("send_input: transmission failed\n");

exit:
	in->i = 0;
	memset(in->txt, '\0', sizeof(in->txt));

	chdir(cwd);
	close(fd);
}

static int
stdin_ready(void)
{
	struct pollfd fds = {0};

	fds.fd = STDIN_FILENO;
	fds.events = POLLIN;
	poll(&fds, 1, 10);

	return fds.revents & POLLIN;
}

static void
handle_selection(struct server *srv, struct inbuf *in, char c)
{
	/* TODO rename: arrows control movement selection */

	in->txt[0] = '\0'; /* TODO memset 0? */
	in->i = 0;

	if (c == CTRL_L) {
		if (srv->chs[srv->i + 1].name[0])
			srv->i++;
	}

	if (c == CTRL_H)
		srv->i ? srv->i-- : 0;

	srv->chs[srv->i].notify = 1;
}

static void
handle_backspace(struct server *srv, struct inbuf *in)
{
	if (in->i)
		--in->i;
	in->txt[in->i] = '\0';
	srv->chs[srv->i].notify = 1;
	/* TODO check all edge cases */
}

static void
insert_input(struct inbuf *in, char c)
{
	/* TODO rename */

	if (in->i >= sizeof(in->txt) - 2) /* TODO better bounds */
		return;

	in->txt[in->i] = c;
	in->i++;
}

static void
handle_input(struct server *srv, struct inbuf *in)
{
	char c;

	if (!stdin_ready())
		return;

	c = getchar();
	if (c == CTRL_H || c == CTRL_L)
		handle_selection(srv, in, c);
	else if (c == BCKSP)
		handle_backspace(srv, in);
	else if (c == '\n' && in->i)
		send_input(srv, in);
	else if (isprint(c))
		insert_input(in, c);
	else
		(void)0; /* TODO what? */

	srv->chs[srv->i].notify = 1;
}

static void
raw_term(void)
{
	struct termios attr;

	tcgetattr(STDIN_FILENO, &attr);
	attr.c_lflag &= ~(ICANON | ECHO);
	tcsetattr(STDIN_FILENO, TCSANOW, &attr);

	/* TODO sufficient with just setbuf */
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

	/* TODO what if to small to fill screen? */

	for (; (n = read(fd, buf, sizeof(buf))); )
		write(STDOUT_FILENO, buf, n);
}

int
read_ready(int fd)
{
	off_t cur;
	off_t end;

	cur = lseek(fd, 0, SEEK_CUR);
	if (cur < 0)
		die("error seeking in file\n");

	end = lseek(fd, 0, SEEK_END);

	lseek(fd, cur, SEEK_SET);
	return end - cur;
}

static void
poll_outputs(struct server *srv)
{
	size_t i;
	size_t len;

	/* TODO poll ALL servers */

	len = sizeof(srv->chs) / sizeof(*srv->chs);
	for (i = 0; i < len && srv->chs[i].name[0]; i++) {
		if (read_ready(srv->chs[i].out))
			srv->chs[i].notify = 1;
	}
}

static void
print_outputs(struct server *srv, char *in)
{
	poll_outputs(srv); /* TODO get bigger data */
	if (srv->chs[srv->i].notify) {
		print_tail(srv->chs[srv->i].out);
		srv->chs[srv->i].notify = 0;

		print_channels(srv);

		printf("in: %s", in);
		fflush(stdout);
	}
}

static void
init_channel(struct server *srv, size_t i, char *name)
{
	char cwd[BUFSIZ];
	strncpy(srv->chs[i].name, name, sizeof(srv->chs[i].name));

	srv->chs[i].notify = 0;

	getcwd(cwd, sizeof(cwd));
		/* TODO don't depend on state */
		/* TODO check return of getcwd */
		/* TODO relative to irc_dir_path or srv->path */
	chdir(name);
	srv->chs[i].out = open("out", O_RDONLY); /* TODO dont create */
	if (srv->chs[i].out < 0)
		die("init_channel: failed to open 'out' file\n");
		/* TODO descriptive err msg */
	chdir(cwd);
}

static void
init_server(struct server *srv, char *name)
{
	strncpy(srv->name, name, sizeof(srv->name));

	init_channel(srv, 0, name);
	srv->chs[0].name[0] = '.';
	srv->chs[0].name[1] = '\0';
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

	if (stat(de->d_name, &sb) < 0) {
		fprintf(stderr,
		        "is_dir: %s %s\n",
		        de->d_name,
			strerror(errno));
		die("is_dir: error on stat()\n");
	}

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
	size_t len;
	struct channel *chs = srv->chs;

	len = sizeof(srv->chs) / sizeof(*srv->chs);
	for (i = 0; i < len; i++) {
		if (chs[i].name[0] == 0) {
			printf("adding %s to %s\n", name, srv->name);
			init_channel(srv, i, name);
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

static void
print_ch_tree(struct server *svs, size_t n)
{
	size_t i, j, chlen;

	printf("svs and chs:\n");

	chlen = sizeof(svs[0].chs) / sizeof(*svs[0].chs);
	for (i = 0; i < n && svs[i].name[0]; i++) {
		printf("  %s\n", svs[i].name);
		for (j = 0; j < chlen && svs[i].chs[j].name[0]; j++) {
			printf("    %s\n", svs[i].chs[j].name);
		}
	}
}

int
main(void)
{
	size_t i;
	struct server servers[MAXSRV] = {0};
	size_t cursrv = 0;
	struct inbuf in = {0};

	find_servers(servers, sizeof(servers)/sizeof(*servers));
	for (i = 0; i < MAXSRV && servers[i].name[0]; i++)
		find_channels(&servers[i]);
	print_ch_tree(servers, sizeof(servers)/sizeof(*servers));

	raw_term();
	for (;;) {
		print_outputs(&servers[cursrv], in.txt);
		handle_input(&servers[cursrv], &in);
		/* TODO periodically check (dis)connections */
	}

	return EXIT_SUCCESS; /* TODO not reachable */
}
