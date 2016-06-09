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

#if 0
	if (strncmp(p, "white-list", 10) == 0) {
		printf("translate whitelist\n");
		set_translate(TRANSLATE_WHITELIST);
	} else if (strncmp(p, "black-list", 10) == 0) {
		printf("translate blacklist\n");
		set_translate(TRANSLATE_BLACKLIST);
	}
#endif

	return;
}

int txdns_create(struct tcpip_info *, struct tcpip_info *);

static void handle_nameserver(char *line)
{
	int count;
	char namlocal[256];
	char namremote[256];
	struct tcpip_info local, remote, fake;

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

static struct command_handler _g_handlers[] = {
	{"fakeip", handle_fakeip},
	{"fakedn", handle_fakedn},
	{"fakenet", handle_fakenet},
	{"localdn", handle_localdn},
	{"localnet", handle_localnet},
	{"domain", handle_domain},

	{"translate", handle_translate},
	{"nameserver", handle_nameserver},

	{"ns-ttl", handle_nsttl},
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

