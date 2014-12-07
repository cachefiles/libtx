#include <stdio.h>
#include <string.h>

struct command_handler {
	char command[64];
	void (*handle)(char *line);
};

static void handle_fakeip(char *line)
{

}

static void handle_fakenet(char *line)
{

}

int ip_isfake(unsigned int ip)
{

	return 0;
}

static void handle_fakedn(char *line)
{

}

static void handle_localdn(char *line)
{

}

static void handle_localnet(char *line)
{

}

int ip_islocal(unsigned int ip)
{
	return 0;
}

static void handle_domain(char *line)
{

}

static void handle_translate(char *line)
{

}

static void handle_fuckingip(char *line)
{

}

static void handle_fuckingnsdectect(char *line)
{

}

static void handle_nameserver(char *line)
{

}

static void handle_nsttl(char *line)
{

}

static void handle_relay(char *line)
{

}

static void handle_listen(char *line)
{

}

static void handle_cachefile(char *line)
{

}

static void handle_dynamic_range(char *line)
{

}

static struct command_handler _g_handlers[] = {
	{"fakeip", handle_fakeip},
	{"fakedn", handle_fakedn},
	{"fakenet", handle_fakenet},
	{"localdn", handle_localdn},
	{"localnet", handle_localnet},
	{"domain", handle_domain},
	{"translate", handle_translate},
	{"fuckingip", handle_fuckingip},
	{"nameserver", handle_nameserver},
	{"fuckingnsdectect", handle_fuckingnsdectect},

	{"ns-ttl", handle_nsttl},
	{"relay", handle_relay},
	{"listen", handle_listen},
	{"cachefile", handle_cachefile},
	{"dynamic-range", handle_dynamic_range},
	{"", NULL}
};

int load_config(const char *path)
{
	int i;
	char line[4096];
	FILE *fp = fopen(path, "r");

	if (fp != NULL) {
		char cmd[64];

		while (fgets(line, sizeof(line), fp)) {
			if (sscanf(line, "%s", cmd) != 1) {
				continue;
			}

			if (*cmd == '#') {
				continue;
			}

			for (i = 0; _g_handlers[i].handle; i++) {
				if (strcmp(cmd, _g_handlers[i].command)) {
					continue;
				}

				_g_handlers[i].handle(line);
				break;
			}
		}

		fclose(fp);
	}

	return 0;
}
