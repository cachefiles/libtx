#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <fstream>
#include <iostream>

#ifdef WIN32
#include <winsock.h>
#endif

#ifndef WIN32
#include <unistd.h>
#include <sys/types.h>          /* See NOTES */
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#define closesocket(s) close(s)
#endif

#include "ncatutil.h"

#define DEBUG(fmt, args...)

#if defined(WIN32)
typedef int socklen_t;
#define inet_pton _xx_inet_pton
#define ABORTON(cond) if (cond) goto clean
static int inet_pton4(const char *src, unsigned char *dst);
static int inet_pton(int af, const char* src, void* dst)
{
	switch (af) {
		case AF_INET:
			return inet_pton4(src, (unsigned char *)dst);

#if defined(AF_INET6)
		case AF_INET6:
			/*return inet_pton6(src, (unsigned char *)dst); */
#endif

		default:
			return 0;
	}
	/* NOTREACHED */
}

int inet_pton4(const char *src, unsigned char *dst)
{
	static const char digits[] = "0123456789";
	int saw_digit, octets, ch;
	unsigned char tmp[sizeof(struct in_addr)], *tp;

	saw_digit = 0;
	octets = 0;
	*(tp = tmp) = 0;
	while ((ch = *src++) != '\0') {
		const char *pch;

		if ((pch = strchr(digits, ch)) != NULL) {
			unsigned int nw = *tp * 10 + (pch - digits);

			if (saw_digit && *tp == 0)
				return 0;
			if (nw > 255)
				return 0;
			*tp = nw;
			if (!saw_digit) {
				if (++octets > 4)
					return 0;
				saw_digit = 1;
			}
		} else if (ch == '.' && saw_digit) {
			if (octets == 4)
				return 0;
			*++tp = 0;
			saw_digit = 0;
		} else
			return 0;
	}
	if (octets < 4)
		return 0;
	memcpy(dst, tmp, sizeof(struct in_addr));
	return 1;
}
#endif

struct netcat_t {
    int l_mode;
    const char *option;
    const char *sai_port;
    const char *sai_addr;
    const char *dai_port;
    const char *dai_addr;
};

/* -------------------------------------------------------------------- */

using namespace std;

static void error_check(int exited, const char *str)
{
    if (exited) {
        DEBUG("%s\n", str);
        exit(-1);
    }

    return;
}

static char* memdup(const void *buf, size_t len)
{
    char *p = (char *)malloc(len);
    if (p != NULL)
        memcpy(p, buf, len);
    return p;
}

int get_cat_socket(netcat_t *upp)
{
    int serv = socket(AF_INET, SOCK_STREAM, 0);

    sockaddr_in my_addr;
    my_addr.sin_family = AF_INET;
    my_addr.sin_port = htons(upp->sai_port? atoi(upp->sai_port): 0);
    if (upp->sai_addr == NULL) {
        my_addr.sin_addr.s_addr = INADDR_ANY;
#if 0
    } else if (inet_pton(AF_INET, upp->sai_addr, &my_addr.sin_addr) <= 0) {
        std::cerr << "incorrect network address.\n";
        return -1;
#endif
    }

    if ((upp->sai_addr != NULL || upp->sai_port != NULL) &&
            (-1 == bind(serv, (sockaddr*)&my_addr, sizeof(my_addr)))) {
        std::cerr << "bind network address.\n";
        return -1;
    }

    if (upp->l_mode) {
        int ret;
        struct sockaddr their_addr;
        socklen_t namlen = sizeof(their_addr);

        ret = listen(serv, 5);
        error_check(ret == -1, "listen");
        DEBUG("server is ready at port: %s\n", upp->sai_port);

        ret = accept(serv, &their_addr, &namlen);
        error_check(ret == -1, "recvfrom failure");

        closesocket(serv);
        return ret;

    } else {
        sockaddr_in their_addr;
        their_addr.sin_family = AF_INET;
        their_addr.sin_port = htons(short(atoi(upp->dai_port)));
        if (inet_pton(AF_INET, upp->dai_addr, &their_addr.sin_addr) <= 0) {
            std::cerr << "incorrect network address.\n";
            closesocket(serv);
            return -1;
        }

        if (-1 == connect(serv, (sockaddr*)&their_addr, sizeof(their_addr))) {
            std::cerr << "connect: " << endl;
            closesocket(serv);
            return -1;
        }

        return serv;
    }

    return -1;
}

netcat_t* get_cat_context(int argc, char **argv)
{
    int i;
    int opt_pidx = 0;
    int opt_listen = 0;
    char *parts[2] = {0};
    static netcat_t _ncat_ctx;
	struct netcat_t *upp = &_ncat_ctx;
	const char *option = 0;
    const char *domain = 0, *port = 0;
    const char *s_domain = 0, *sai_port = 0;

    for (i = 1; i < argc; i++) {
        if (!strcmp("-l", argv[i])) {
            opt_listen = 1;
        } else if (!strcmp("-o", argv[i])) {
            error_check(++i == argc, "-o need an argument");
            option = argv[i];
        } else if (!strcmp("-s", argv[i])) {
            error_check(++i == argc, "-s need an argument");
            s_domain = argv[i];
        } else if (!strcmp("-p", argv[i])) {
            error_check(++i == argc, "-p need an argument");
            sai_port = argv[i];
        } else if (opt_pidx < 2) {
            parts[opt_pidx++] = argv[i];
        } else {
            DEBUG("too many argument");
            return 0;
        }
    }

    if (opt_pidx == 1) {
        port = parts[0];
        for (i = 0; port[i]; i++) {
            if (!isdigit(port[i])) {
                domain = port;
                port = NULL;
                break;
            }
        }
    } else if (opt_pidx == 2) {
        port = parts[1];
        domain = parts[0];
        for (i = 0; domain[i]; i++) {
            if (!isdigit(domain[i])) {
                break;
            }
        }

        error_check(domain[i] == 0, "should give one port only");
    }

    if (opt_listen) {
        if (s_domain != NULL)
            error_check(domain != NULL, "domain repeat twice");
        else
            s_domain = domain;

        if (sai_port != NULL)
            error_check(port != NULL, "port repeat twice");
        else
            sai_port = port;
    } else {
        u_long f4wardai_addr = 0;
        error_check(domain == NULL, "hostname is request");
        f4wardai_addr = inet_addr(domain);
        error_check(f4wardai_addr == INADDR_ANY, "bad hostname");
        error_check(f4wardai_addr == INADDR_NONE, "bad hostname");
    }

    upp->option = option;
    upp->l_mode = opt_listen;
    upp->sai_addr = s_domain;
    upp->sai_port = sai_port;
    upp->dai_addr = domain;
    upp->dai_port = port;
    return upp;
}

const char *get_cat_options(netcat_t *upp, const char *name)
{
	char *key, *val;
	static char options[8192];

	if (upp->option != NULL) {
		strncpy(options, upp->option, sizeof(options));
		options[sizeof(options) - 1]= 0;

		val = "enabled";
		key = options;
		/* len = strlen(name); */

		for (int i = 0; options[i]; i++) {
			if (options[i] == '=') {
				options[i] = 0;
				val = &options[i + 1];
			} else if (options[i] == ',') {
				options[i] = 0;
				if (strcmp(name, key) == 0) {
					/* XXX */
					return val;
				}
				key = &options[i + 1];
				val = "enabled";
			}
		}

		if (strcmp(name, key) == 0) {
			/* XXX */
			return val;
		}
	}

	return NULL;
}

int get_netcat_socket(int argc, char *argv[])
{
	netcat_t* upp = get_cat_context(argc, argv);
	if (upp == NULL) {
		perror("get_cat_context");
		return -1;
	}

	int nfd = get_cat_socket(upp);
	if (nfd < 0) {
		perror("net_create");
		return -1;
	}

    return nfd;
}
