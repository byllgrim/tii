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
	int selected; /* TODO use index variable instead */
	int notify;
};

static void
print_channels(struct channel *ch) /* TODO assuming MAXCH */
{
	size_t i;

	printf("ch: ");
	for (i = 0; i < MAXCHN; i++) {
		if (ch[i].selected)
			printf("[%s]", ch[i].name);
		else
			printf(" %s%c",
			       ch[i].name,
			       ch[i].notify ? '*' : ' ');
	}
	puts("");
}

static void
send_input(char *msg, size_t n, struct channel *ch, size_t i)
{
	FILE *f;
	char p[BUFSIZ]; /* TODO too big size */

	p[0] = '\0';
	strcat(p, "./");
	strcat(p, ch[0].name);
	strcat(p, "/in");
	/* strcat(p, ""); TOOD if !i */
	(void)i; /* TODO don't default to 0 */
	/* TODO check if file exist */
	/* TODO check if ii is running */
	if (!(f = fopen(p, "a"))) {
		printf("send_input: failed to open file\n");
		/* TODO proper error checking */
		return;
	}

	/* TODO proper error checking */
	if (fwrite(msg, sizeof(char), n, f))
		printf("sent success\n");
	else
		printf("boo\n");

	fwrite("\n", 1, 1, f); /* TODO don't strip \n */
	printf("sent '%s' to '%s'\n", msg, p);
	fclose(f);
}

static void
get_input(char *buf, size_t n)
{
	char c;
	size_t i;

	for (i = 0; i < n - 1; i++) {
		c = getchar(); /* TODO don't block output */

		if (c == CTRL_L) {
			printf("next channel\n");
			i--;
			continue;
		} /* TODO else */

		if (c == CTRL_H) {
			printf("prev channel\n");
			i--;
			continue;
		}

		if (c == '\n')
			c = '\0';

		buf[i] = c;

		if (c == '\0')
			return; /* TODO better logic */
	}
}

static void
non_canonical(void)
{
	struct termios attr;

	tcgetattr(0, &attr); /* TODO no magic numbers */
	attr.c_lflag &= ~ICANON;
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
	ch[0].selected = 1; /* TODO use index instead */

	/* TODO refactor */
	non_canonical();
	while (1) {
		cmd[0] = '\0';
		strcat(cmd, "tail -n24 ./"); /* TODO -n = ? */
		strcat(cmd, ch[0].name);
		strcat(cmd, "/out");
		system(cmd);
		/* TODO use read() */

		print_channels(ch);

		printf("in: ");
		get_input(in, sizeof(in));
		if (in[0])
			send_input(in, strlen(in), ch, 0);
		/* TODO input don't block output */
	}

	return 0;
}