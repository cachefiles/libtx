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
#include "txdnsxy.h"
#include "txconfig.h"

struct command_handler {
	char command[64];
	void (*handle)(char *line);
};

static void handle_fakeip(char *line)
{
	unsigned int addr;
	char *p, delim[] = " ";

	p = strtok(line, delim);

	p = strtok(NULL, delim);
	if (p == NULL) {
		printf("fakeip need a argument\n");
		return;
	}

	addr = inet_addr(p);
	add_fakeip(addr);
	return;
}

static void handle_fakenet(char *line)
{
	int mask = 32;
	unsigned int addr;
	char *p, *dot, delim[] = " ";

	p = strtok(line, delim);

	p = strtok(NULL, delim);
	if (p == NULL) {
		printf("fakenet need a argument\n");
		return;
	}

	dot = strchr(p, '/');
	if (dot != NULL) {
		*dot++ = 0;
		mask = atoi(dot);
	}

	addr = inet_addr(p);
	add_fakenet(addr, (1 << (32 - mask)) - 1);
	return;
}

static void handle_fakedn(char *line)
{
	unsigned int addr;
	char *t, cache[256];
	char *p, delim[] = " ";

	p = strtok(line, delim);

	p = strtok(NULL, delim);
	if (p == NULL) {
		printf("fakedn need a argument\n");
		return;
	}

	t = cache;
	if (*p == '\'') p++;
	if (*p == '\"') p++;

	while (*p != '\"' && *p != '\"'
			&& *p != 0 && *p != ' ') {
		*t++ = *p++;
	}

	*t++ = 0;
	add_fakedn(cache);
	return;
}

static void handle_localdn(char *line)
{
	unsigned int addr;
	char *t, cache[256];
	char *p, delim[] = " ";

	p = strtok(line, delim);

	p = strtok(NULL, delim);
	if (p == NULL) {
		printf("localdn need a argument\n");
		return;
	}

	t = cache;
	if (*p == '\'') p++;
	if (*p == '\"') p++;

	while (*p != '\"' && *p != '\"'
			&& *p != 0 && *p != ' ') {
		*t++ = *p++;
	}

	*t++ = 0;
	add_localdn(cache);
	return;
}

static void handle_localnet(char *line)
{
	int mask = 32;
	unsigned int addr;
	char *p, *dot, delim[] = " ";

	p = strtok(line, delim);

	p = strtok(NULL, delim);
	if (p == NULL) {
		printf("localnet need a argument\n");
		return;
	}

	dot = strchr(p, '/');
	if (dot != NULL) {
		*dot++ = 0;
		mask = atoi(dot);
	}

	addr = inet_addr(p);
	add_localnet(addr, (1 << (32 - mask)) - 1);
	return;
}

static void handle_domain(char *line)
{
	unsigned int addr;
	char *t, domain[256];
	char *p, delim[] = " ";

	p = strtok(line, delim);

	p = strtok(NULL, delim);
	if (p == NULL) {
		printf("domain need two argument\n");
		return;
	}

	t = domain;
	if (*p == '\'') p++;
	if (*p == '\"') p++;

	while (*p != '\"' && *p != '\"'
			&& *p != 0 && *p != ' ') {
		*t++ = *p++;
	}

	*t++ = 0;

	p = strtok(NULL, delim);
	if (p == NULL) {
		printf("domain need two argument\n");
		return;
	}

	addr = inet_addr(p);
	fprintf(stderr, "domain %s %x\n", domain, addr);
	add_domain(domain, addr);
	return;
}

static void handle_translate(char *line)
{
	unsigned int addr;
	char *p, delim[] = " ";

	p = strtok(line, delim);

	p = strtok(NULL, delim);
	if (p == NULL) {
		printf("translate need a argument\n");
		return;
	}

	if (strncmp(p, "white-list", 10) == 0) {
		printf("translate whitelist\n");
		set_translate(TRANSLATE_WHITELIST);
	} else if (strncmp(p, "black-list", 10) == 0) {
		printf("translate blacklist\n");
		set_translate(TRANSLATE_BLACKLIST);
	}

	return;
}

static void handle_fuckingip(char *line)
{
	unsigned int addr;
	char *p, delim[] = " ";

	p = strtok(line, delim);

	p = strtok(NULL, delim);
	if (p == NULL) {
		printf("fuckingip need a argument\n");
		return;
	}

	addr = inet_addr(p);
	set_fuckingip(addr);
	return;
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
	fprintf(stderr, "nsttl not supported yet!\n");
	return;
}

int set_default_relay(const char *url, const char *user, const char *password);

static void handle_relay(char *line)
{
	int count = 0;
	int change = 0;
	char user[256];
	char relay[256];
	char password[256];
	char *p, delim[] = " ";

	do {
		p = strtok(line, delim);
		if (p == NULL) break;

		p = strtok(NULL, delim);
		if (p == NULL) break;

		fprintf(stderr, "relay server %s\n", p);
		strncpy(relay, p, sizeof(relay));
		count = 1;

		p = strtok(NULL, delim);

		while (p != NULL) {
			if (strcmp(p, "dynamic") == 0) {
				/* TODO: add some handle */
				fprintf(stderr, "group subcommand not supported yet\n");
			} else if (strcmp(p, "user") == 0) {
				p = strtok(NULL, delim);
				if (p != NULL) {
					char *t = user;
					if (*p == '\'') p++;
					if (*p == '\"') p++;

					while (*p != '\"' && *p != '\"'
							&& *p != 0 && *p != ' ') {
						*t++ = *p++;
					}

					*t++ = 0;
				}
				change = 1;
			} else if (strcmp(p, "password") == 0) {
				p = strtok(NULL, delim);
				if (p != NULL) {
					char *t = password;
					if (*p == '\'') p++;
					if (*p == '\"') p++;

					while (*p != '\"' && *p != '\"'
							&& *p != 0 && *p != ' ') {
						*t++ = *p++;
					}

					*t++ = 0;
				}
				change = 1;
			}

			p = strtok(NULL, delim);
		}

	} while ( 0);

final_step:
	if (count == 1) {
		set_default_relay(relay, user, password);
		return;
	}

	return;
}

void * txlisten_create(struct tcpip_info *info);
void * txlisten_setport(void *up, int port);
void * txlisten_addflags(void *up, int flags);
void * txlisten_addredir(void *up, const char *redir);

static void handle_listen(char *line)
{
	int count = 0;
	char saname[256];
	struct tcpip_info lsa0 = {0};

	char *p, delim[] = " ";

	do {
		p = strtok(line, delim);
		if (p == NULL) break;

		p = strtok(NULL, delim);
		if (p == NULL) break;

		get_target_address(&lsa0, p);
		void *up = txlisten_create(&lsa0);

		p = strtok(NULL, delim);

		while (p != NULL) {

			if (strcmp(p, "dynamic") == 0) {
				txlisten_addflags(up, DYNAMIC_TRANSLATE);
			} else if (strcmp(p, "redir") == 0) {
				p = strtok(NULL, delim);
				txlisten_addredir(up, p);
			} else if (strcmp(p, "ns-ttl") == 0) {
				p = strtok(NULL, delim);
				fprintf(stderr, "ns-ttl not supported %s\n", p);
			} else if (strcmp(p, "port") == 0) {
				p = strtok(NULL, delim);
				txlisten_setport(up, htons(atoi(p)));
			}

			p = strtok(NULL, delim);
		}

	} while ( 0);

	return;
}

static void handle_dynamic_range(char *line)
{
	int count;
	char ipstart[256];
	char iplimit[256];

	count = sscanf(line, "%*s %s %s", ipstart, iplimit);
	if (count == 2) {
		unsigned ip0, ip9;
		fprintf(stderr, "dynamic-range: %s %s\n", ipstart, iplimit);
		ip0 = inet_addr(ipstart);
		ip9 = inet_addr(iplimit);
		set_dynamic_range(ip0, ip9);
		return;
	}

	fprintf(stderr, "dynamic-range failure %d\n%s", count, line);
	return;
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
