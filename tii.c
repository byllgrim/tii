#define _DEFAULT_SOURCE /* TODO more conservative */

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
	NAMELEN = 128,
	MAXCHN = 16
};

struct channel {
	char name[NAMELEN];
	int selected; /* TODO use index variable instead */
	int notify;
};

static void
print_channels(struct channel *ch)
{
	printf("[%s]\n", ch[0].name);
	/* TODO print all channels */
}

int
main(void)
{
	DIR *d;
	struct dirent *ds;
	struct channel ch[MAXCHN];
	size_t i;
	char cmd[BUFSIZ];

	/* TODO explicit_bzero the array of channels */

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

	 while (1) {
		cmd[0] = '\0';
		strcat(cmd, "tail -n24 ./");
		strcat(cmd, ch[0].name);
		strcat(cmd, "/out");
		system(cmd);

		print_channels(ch);

		printf("in: ");
		getchar();
	}

	return 0;
}
