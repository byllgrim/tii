#include <sys/stat.h>

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

enum {
	NAMELEN = 128,
	MAXSRV = 16,
	MAXCHN = 32,
	CTRL_L = 0x0C,
	CTRL_H = 0x08,
	CTRL_N = 0x0E,
	CTRL_P = 0x10,
	CTRL_U = 0x15,
	BCKSP = 0x7f,
	ROWS = 24,
	COLS = 80
};

struct inbuf {
	char txt[BUFSIZ]; /* TODO smaller buffer? */
	size_t i;
};

struct channel {
	char name[NAMELEN];
	char notify;
	char active;
	int out;
};

struct server {
	char name[NAMELEN];
	struct channel chs[MAXCHN];
	size_t i;
};

struct srv_list {
	struct server svs[MAXSRV];
	size_t i;
};

static void
die(char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);

	exit(EXIT_FAILURE);
}

static void
print_channels(struct server *srv)
{
	size_t i;
	char *name;

	/* TODO protect against column overflow */

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
clear_input(struct inbuf *in)
{
	in->i = 0;
	memset(in->txt, '\0', sizeof(in->txt));
}

static int
is_all_blanks(struct inbuf *in)
{
	size_t i;

	/* TODO is_valid_inbuf() */
	if (in->txt[sizeof(in->txt) - 1] != '\0')
		die("is_all_blanks: corrupt C string\n");

	for (i = 0; i < strlen(in->txt); i++) {
		if (!isspace(in->txt[i]))
			return 0;
	}

	return 1;
}

static void
send_input(struct server *srv, struct inbuf *in)
{
	char cwd[BUFSIZ];
	int fd;
	ssize_t n;

	if (is_all_blanks(in))
		return;

	/* TODO refactor */
	if (!getcwd(cwd, sizeof(cwd)))
		die("send_input: failed to get cwd\n");
	if (chdir(srv->name) < 0)
		die("send_input: cant chdir to %s\n", srv->name);
	if (chdir(srv->chs[srv->i].name) < 0)
		die("send_input: cant to chdir to %s\n",
		    srv->chs[srv->i].name);

	fd = open("in", O_WRONLY | O_NONBLOCK);
		/* TODO keep open all the time? */
		/* TODO die if not existing/connected */
		/* TODO O_APPEND? */
	if (fd < 0) {
		/* TODO visually indicate ENXIO? */
		if (errno == ENXIO)
			goto exit;
		else
			die("send_input: cannot open 'in' file\n");
	}

	n = write(fd, in->txt, sizeof(in->txt));
	if (n != sizeof(in->txt))
		die("send_input: transmission failed\n");

exit:
	clear_input(in);
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
change_channel(struct server *srv, struct inbuf *in, char c)
{
	clear_input(in); /* TODO don't clear if not changing */

	if (c == CTRL_L) {
		if (srv->chs[srv->i + 1].name[0])
			srv->i++;

		/* TODO check bounds */
	}

	if (c == CTRL_H)
		srv->i ? srv->i-- : 0;

	srv->chs[srv->i].notify = 1;
}

static void
erase_character(struct inbuf *in)
{
	/* TODO check all edge cases */

	if (in->i)
		--in->i;
	in->txt[in->i] = '\0';
}

static void
append_input(struct inbuf *in, char c)
{
	if (in->i >= sizeof(in->txt) - 2) /* TODO better bounds */
		return;

	in->txt[in->i] = c;
	in->i++;
}

static void
change_server(struct srv_list *sl, char c)
{
	if (c == CTRL_N) {
		if (sl->svs[sl->i + 1].name[0])
			sl->i++;

		/* TODO check bounds */
	}

	if (c == CTRL_P) {
		if (sl->i)
			sl->i--;
	}
}

static void
handle_input(struct srv_list *sl, struct inbuf *in)
{
	struct server *srv = &sl->svs[sl->i];
	char c;

	if (!stdin_ready())
		return;

	/* TODO protect against column overflow? */

	c = getchar();
	if (c == CTRL_H || c == CTRL_L)
		change_channel(srv, in, c);
	else if (c == CTRL_N || c == CTRL_P)
		change_server(sl, c);
	else if (c == CTRL_U)
		clear_input(in);
	else if (c == BCKSP)
		erase_character(in);
	else if (c == '\n' && in->i)
		send_input(srv, in);
	else if (isprint(c))
		append_input(in, c);
	else
		fprintf(stderr, "handle_input: invalid char\n");

	srv->chs[srv->i].notify = 1;
}

static void
raw_term(void)
{
	struct termios attr;

	tcgetattr(STDIN_FILENO, &attr);
	attr.c_lflag &= ~(ICANON | ECHO);
	tcsetattr(STDIN_FILENO, TCSANOW, &attr);
}

static void
print_tail(int fd)
{
	off_t pos, end;
	char buf[BUFSIZ]; /* TODO too big? */
	size_t a, b;

	pos = end = lseek(fd, 0, SEEK_END);
	pos -= ROWS*COLS;
	if (pos < 0)
		pos = 0;
	lseek(fd, pos, SEEK_SET);

	/* TODO less complicated logic */
	a = ROWS*COLS - (end - pos);
	a = (a > BUFSIZ) ? BUFSIZ : a;
	memset(buf, ' ', a);
	buf[a - 1] = '\n';

	b = read(fd, buf + a, sizeof(buf) - a); /* TODO check return */
	write(STDOUT_FILENO, buf, a + b);
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

	/* TODO what if already initialized? */

	strncpy(srv->chs[i].name, name, sizeof(srv->chs[i].name));
	srv->chs[i].notify = 0;
	srv->chs[i].active = 1;

	getcwd(cwd, sizeof(cwd));
		/* TODO don't depend on state */
		/* TODO check return */
		/* TODO relative to irc_dir_path or srv->path */
	chdir(name); /* TODO check return */
	srv->chs[i].out = open("out", O_RDONLY); /* TODO dont create */
	if (srv->chs[i].out < 0)
		die("init_channel: can't open %ss 'out' file\n", name);

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
		die("is_dir: error on stat(%s)\n", de->d_name);

	return S_ISDIR(sb.st_mode);
}

static void
find_servers(struct server *svs, size_t n)
{
	DIR *ds;
	struct dirent *de;

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
			init_channel(srv, i, name);
			return;
		}

		if (!strncmp(chs[i].name, name, sizeof(chs[i].name))) {
			chs[i].active = 1;
			return;
		}
	}
}

static void
clear_inactive_flags(struct server *srv)
{
	size_t i, len;

	len = sizeof(srv->chs) / sizeof(*srv->chs);
	for (i = 1; i < len && srv->chs[i].name[0]; i++) {
		srv->chs[i].active = 0;
	}
}

static void
remove_channel(struct server *srv, size_t n)
{
	size_t i, len;

	len = sizeof(srv->chs) / sizeof(*srv->chs);
	for (i = n; i < len - 1; i++)
		srv->chs[i] = srv->chs[i + 1];

	/* TODO what about last ch? */

	if (srv->i >= n)
		srv->i = n - 1;
	srv->chs[srv->i].notify = 1;
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

static void
remove_inactives(struct server *srv)
{
	size_t i, len;

	len = sizeof(srv->chs) / sizeof(*srv->chs);
	for (i = 0; i < len && srv->chs[i].name[0]; i++) {
		if (!srv->chs[i].active)
			remove_channel(srv, i);
	}
}

static void
find_channels(struct server *srv)
{
	DIR *ds;
	struct dirent *de;

	ds = opendir(srv->name);
	if (!ds)
		die("find_channels: failed to open server directory\n");

	if (chdir(srv->name) < 0)
		die("find_channels: can't chdir\n");

	clear_inactive_flags(srv); /* TODO more elegant solution */

	/* TODO dry from find_servers */
	errno = 0;
	for (; (de = readdir(ds)); ) {
		if (is_dir(de) && de->d_name[0] != '.')
			add_channel(srv, de->d_name);
	}
	if (errno)
		die("find_channels: error reading directory entries\n");

	remove_inactives(srv);
	chdir(".."); /* TODO getcwd() */
	closedir(ds);
}

int
main(void)
{
	struct srv_list sl = {0};
	struct inbuf in = {0};

	find_servers(sl.svs, sizeof(sl.svs)/sizeof(*sl.svs));
	/* TODO reset notifies on first run? */
	print_ch_tree(sl.svs, sizeof(sl.svs)/sizeof(*sl.svs));

	raw_term();
	for (;;) {
		find_channels(&sl.svs[sl.i]);
		print_outputs(&sl.svs[sl.i], in.txt);
		handle_input(&sl, &in);
	}
}
