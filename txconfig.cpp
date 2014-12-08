#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

#ifndef WIN32
#include <netdb.h>
#else
#include <winsock.h>
#endif

#include "txall.h"
#include "txconfig.h"

int get_target_address(struct tcpip_info *info, const char *address)
{
	const char *last;

#define FLAG_HAVE_DOT    1
#define FLAG_HAVE_ALPHA  2
#define FLAG_HAVE_NUMBER 4
#define FLAG_HAVE_SPLIT  8

	int flags = 0;
	char host[128] = {};

	for (last = address; *last; last++) {
		if (isdigit(*last)) flags |= FLAG_HAVE_NUMBER;
		else if (*last == ':') flags |= FLAG_HAVE_SPLIT;
		else if (*last == '.') flags |= FLAG_HAVE_DOT;
		else if (isalpha(*last)) flags |= FLAG_HAVE_ALPHA;
		else { fprintf(stderr, "get target address failure!\n"); return -1;}
	}

	if (flags == FLAG_HAVE_NUMBER) {
		info->port = htons(atoi(address));
		return 0;
	}

	if (flags == (FLAG_HAVE_NUMBER| FLAG_HAVE_DOT)) {
		info->address = inet_addr(address);
		return 0;
	}

	struct hostent *host0 = NULL;
	if ((flags & ~FLAG_HAVE_NUMBER) == (FLAG_HAVE_ALPHA | FLAG_HAVE_DOT)) {
		host0 = gethostbyname(address);
		if (host0 != NULL)
			memcpy(&info->address, host0->h_addr, 4);
		return 0;
	}

	if (flags & FLAG_HAVE_SPLIT) {
		const char *split = strchr(address, ':');
		info->port = htons(atoi(split + 1));

		if (strlen(address) < sizeof(host)) {
			strncpy(host, address, sizeof(host));
			host[split - address] = 0;

			if (flags & FLAG_HAVE_ALPHA) {
				host0 = gethostbyname(host);
				if (host0 != NULL)
					memcpy(&info->address, host0->h_addr, 4);
				return 0;
			}

			info->address = inet_addr(host);
		}
	}

	return 0;
}

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

static void handle_fakedn(char *line)
{

}

static void handle_localdn(char *line)
{

}

static void handle_localnet(char *line)
{

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

int txdns_create(struct tcpip_info *, struct tcpip_info *);

static void handle_nameserver(char *line)
{
    int count;
    char namlocal[256];
    char namremote[256];
    struct tcpip_info local, remote;

    count = sscanf(line, "%*s %s %s", namlocal, namremote);
    if (count == 2) {
        printf("nameserver: %s %s\n", namlocal, namremote);
        get_target_address(&remote, namremote);
        get_target_address(&local, namlocal);
        txdns_create(&local, &remote);
        return;
    }

    fprintf(stderr, "nameserver failure %d\n%s", count, line);
    return;
}

static void handle_nsttl(char *line)
{

}

static void handle_relay(char *line)
{

}

void txlisten_create(struct tcpip_info *info);

#define DYNAMIC_TRANSLATE 0x01

struct listen_config {
    int nsttl;
    int flags;
    int group;
    unsigned short port;
};

static void handle_listen(char *line)
{
    int count = 0;
    int change = 0;
    char saname[256];
    struct tcpip_info lsa0;
    struct listen_config cfg;

    char *p, delim[] = " ";

    do {
        cfg.flags = 0;

        p = strtok(line, delim);
        if (p == NULL) break;

        p = strtok(NULL, delim);
        if (p == NULL) break;

        get_target_address(&lsa0, p);
        cfg.port = lsa0.port;
        count = 1;

        p = strtok(NULL, delim);

        while (p != NULL) {

            if (strcmp(p, "dynamic") == 0) {
                cfg.flags |= DYNAMIC_TRANSLATE;
                change = 1;
            } else if (strcmp(p, "redir") == 0) {
                p = strtok(NULL, delim);
                strdup(p);
                change = 1;
            } else if (strcmp(p, "ns-ttl") == 0) {
                p = strtok(NULL, delim);
                cfg.nsttl = atoi(p);
                change = 1;
            } else if (strcmp(p, "port") == 0) {
                p = strtok(NULL, delim);
                cfg.port = atoi(p);
                change = 1;
            }

            p = strtok(NULL, delim);
        }

    } while ( 0);

final_step:
    if (count == 1) {
        txlisten_create(&lsa0);
        return;
    }

    fprintf(stderr, "listen failure %d\n%s", count, line);
    return;
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
        return 0;
	}

    fprintf(stderr, "open config file: %s failure\n", path);
	return 0;
}
