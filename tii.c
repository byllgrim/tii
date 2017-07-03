#define _DEFAULT_SOURCE /* TODO more conservative */

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

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
print_channels(struct channel *ch, size_t idx) /* TODO assuming MAXCH */
{
	size_t i;

	printf("ch: ");
	for (i = 0; i < MAXCHN; i++) {
		if (i == idx)
			printf("[%s]", ch[i].name);
		else
			printf(" %s%c",
			       ch[i].name,
			       ch[i].notify ? '*' : ' ');
	}
	puts("");
}

static void
send_input(char *msg, size_t n, struct channel *ch, size_t idx)
{
	FILE *f;
	char p[BUFSIZ]; /* TODO too big size */

	p[0] = '\0';
	strcat(p, "./");
	strcat(p, ch[0].name);
	if (idx) {
		strcat(p, "/");
		strcat(p, ch[idx].name);
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

	fwrite("\n", 1, 1, f); /* TODO don't strip \n */
	printf("sent '%s' to '%s'\n", msg, p);
	fclose(f);
}

static void
get_input(char *buf, size_t n, size_t *idx)
{
	char c;
	size_t i;

	for (i = 0; i < n - 1; i++) {
		/* TODO don't wait if no input */
		/* TODO loop many times; don't hang */
		c = getchar(); /* TODO don't block output */
		/* TODO no echo */

		if (c == CTRL_L) {
			(*idx)++; /* TODO check limit */
			if (!i)
				return;
			i--;
			continue;
		} /* TODO else */

		if (c == CTRL_H) {
			*idx ? (*idx)-- : 0;
			if (!i)
				return;
			i--;
			continue;
			/* TODO better ctrl policy */
		}

		/* TODO check printable */

		if (c == '\n')
			c = '\0';

		buf[i] = c;

		if (c == '\0')
			return; /* TODO better logic */
	}
}

static void
raw_term(void)
{
	struct termios attr;

	tcgetattr(0, &attr); /* TODO no magic numbers */
	attr.c_lflag &= ~(ICANON | ECHO);
	tcsetattr(0, TCSANOW, &attr);
}

int
main(void)
{
	DIR *d;
	struct dirent *ds;
	struct channel ch[MAXCHN] = {0};
	size_t i;
	char cmd[BUFSIZ];
	char in[BUFSIZ];
	size_t idx;

	/* TODO refactor into parse_dirs() */
	d = opendir("./"); /* TODO error checking */
	ch[0].name[0] = '\0';
	for (i = 0; (ds = readdir(d)); ) {
		if (ds->d_type != DT_DIR || ds->d_name[0] == '.')
			continue;

		if (!ch[0].name[0]) {
			strncpy(ch[0].name, ds->d_name, NAMELEN);
			/* TODO null termination */
			printf("server: %s\n", ch[0].name);
			d = opendir(ch[0].name); /* TODO error checking */
		} else {
			strncpy(ch[i].name, ds->d_name, NAMELEN);
			/* TODO null termination */
			/* TODO duplication */
			printf("channel: %s\n", ch[i].name);
		}

		i++;
	}
	idx = 0;

	/* TODO refactor */
	raw_term();
	while (1) {
		cmd[0] = '\0';
		strcat(cmd, "tail -n24 ./"); /* TODO -n = ? */
		strcat(cmd, ch[0].name);
		if (idx) {
			strcat(cmd, "/");
			strcat(cmd, ch[idx].name);
		}
		strcat(cmd, "/out");
		system(cmd);
		/* TODO use read() */

		print_channels(ch, idx);

		printf("in: ");
		get_input(in, sizeof(in), &idx);
		if (in[0])
			send_input(in, strlen(in), ch, idx);
		/* TODO input don't block output */
	}

	return 0;
}
