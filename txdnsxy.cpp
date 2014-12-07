#include <ctype.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>

#ifdef WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <netdb.h>
#define closesocket close
#endif

#include "txall.h"


struct dns_query_packet {
	unsigned short q_ident;
	unsigned short q_flags;
	unsigned short q_qdcount;
	unsigned short q_ancount;
	unsigned short q_nscount;
	unsigned short q_arcount;
};

#if 0
QR: 1;
opcode: 4;
AA: 1;
TC: 4;
RD: 1;
RA: 1;
zero: 3;
rcode: 4;
#endif

const char * dns_extract_name(char * name, size_t namlen,
		const char * dnsp, const char * finp)
{
	int partlen;
	char nouse = '.';
	char * lastdot = &nouse;

	if (dnsp == finp)
		return finp;

	partlen = (unsigned char)*dnsp++;
	while (partlen) {
		unsigned short offset = 0;

		if (partlen & 0xC0) {
			offset = ((partlen & 0x3F) << 8);
			offset = (offset | *++dnsp);
			break;
		}

		if (dnsp + partlen > finp)
			return finp;

		if (namlen > partlen + 1) {
			memcpy(name, dnsp, partlen);
			namlen -= partlen;
			name += partlen;
			dnsp += partlen;

			lastdot = name;
			*name++ = '.';
			namlen--;
		}

		if (dnsp == finp)
			return finp;
		partlen = (unsigned char)*dnsp++;
	}

	*lastdot = 0;
	return dnsp;
}

const char * dns_extract_value(void * valp, size_t size,
		const char * dnsp, const char * finp)
{
	if (dnsp + size > finp)
		return finp;

	memcpy(valp, dnsp, size);
	dnsp += size;
	return dnsp;
}

char * dns_copy_name(char *outp, const char * name)
{
	int count = 0;
	char * lastdot = outp++;

	while (*name) {
		if (*name == '.') {
			assert(count > 0 && count < 64);
			*lastdot = count;
			name++;

			lastdot = outp++;
			count = 0;
			continue;
		}

		*outp++ = *name++;
		count++;
	}

	*lastdot = count;
	*outp++ = 0;

	return outp;
}

char * dns_copy_value(char *outp, void * valp, size_t count)
{
	memcpy(outp, valp, count);
	return (outp + count);
}

static struct cached_client {
	int flags;
	unsigned short r_ident;
	unsigned short l_ident;

	union {
		struct sockaddr sa;
		struct sockaddr_in in0;
	} from;
} __cached_client[512];

static int __last_index = 0;

struct named_item {
	char ni_name[256];
	unsigned int ni_local;
	unsigned int ni_rcvtime;
	struct named_item *ni_next;
};

static int _next_local = 2;
static struct named_item *_named_list_h = NULL;

unsigned int get_wrap_ip(const char *name)
{
	char *p, cached[256];
	struct named_item *item = 0;

	for (item = _named_list_h; item; item = item->ni_next) {
		if (strcmp(name, item->ni_name) == 0) {
			item->ni_rcvtime = tx_getticks();
			return item->ni_local;
		}
	}

	item = new named_item;
	strcpy(item->ni_name, name);
	item->ni_local = _next_local++;
	item->ni_rcvtime = tx_getticks();
	item->ni_next = _named_list_h;
	_named_list_h = item;
	return item->ni_local;
}

/* get cache name by addr */
const char *get_unwrap_name(unsigned int addr)
{
	struct named_item *item;

	for (item = _named_list_h; item; item = item->ni_next) {
		if (item->ni_local == addr) {
			return item->ni_name;
		}
	}

	return NULL;
}

int add_localnet(unsigned int network, unsigned int netmask)
{
	return 0;
}

static int is_localip(const void *valout)
{
	return 0;
}

static const char * _localdn_matcher[] = {
	NULL
};

static int is_localdn(const char *name)
{
	return 0;
}

int add_fakeip(unsigned int ip)
{
	return 0;
}

int add_fakenet(unsigned int network, unsigned int netmask)
{
	return 0;
}

static int is_fakeip(const void *valout)
{
	return 0;
}

static const char * _fakedn_matcher[] = {
	NULL
};

static int is_fakedn(const char *name)
{
	return 0;
}

static int is_fuckingip(void *valout)
{
	unsigned char fucking_dns[] = {0xdc, 0xfa, 0x40, 0xe4};
	return memcmp(fucking_dns, valout, 4) == 0;
}

static int in_translate_whitelist()
{
	return 0;
}

static int in_translate_blacklist()
{
	return 0;
}

static int get_cached_query(const char *name, unsigned short dnstyp, unsigned short dnscls, char *buf, size_t len)
{
	int i;
	int anscount;
	char *outp = NULL;
	unsigned short d_len = 0;
	unsigned int   d_ttl = htonl(3600);
	unsigned int   d_dest = 0;
	struct dns_query_packet *dnsp, *dnsoutp;

	dnsoutp = (struct dns_query_packet *)buf;
	dnsoutp->q_flags = ntohs(0x8180);
	dnsoutp->q_qdcount = ntohs(1);
	dnsoutp->q_nscount = ntohs(0);
	dnsoutp->q_arcount = ntohs(0);

	outp = (char *)(dnsoutp + 1);
	outp = dns_copy_name(outp, name);
	outp = dns_copy_value(outp, &dnstyp, sizeof(dnstyp));
	outp = dns_copy_value(outp, &dnscls, sizeof(dnscls));

	anscount = 0;
	outp = dns_copy_name(outp, name);
	outp = dns_copy_value(outp, &dnstyp, sizeof(dnstyp));
	outp = dns_copy_value(outp, &dnscls, sizeof(dnscls));

	d_len = htons(sizeof(d_dest));
	d_dest = get_wrap_ip(name);
	TX_PRINT(TXL_DEBUG, "return new forward: %s\n", name);

	outp = dns_copy_value(outp, &d_ttl, sizeof(d_ttl));
	outp = dns_copy_value(outp, &d_len, sizeof(d_len));
	outp = dns_copy_value(outp, &d_dest, sizeof(d_dest));
	anscount++;

	TX_PRINT(TXL_DEBUG, "anscount: %d\n", anscount);
	dnsoutp->q_ancount = ntohs(anscount);
	return (anscount > 0? (outp - buf): -1);
}

struct dns_udp_context_t {
	int sockfd;
	tx_aiocb file;

	int outfd;
	tx_aiocb outgoing;

	tx_task_t task;
	struct tcpip_info forward;
};

int dns_forward(dns_udp_context_t *up, char *buf, size_t count, struct sockaddr_in *in_addr1, socklen_t namlen)
{
	int err;
	int flags;
	char name[512];
	const char *queryp;
	const char *finishp;
	unsigned short type, dnscls;
	struct cached_client *client;
	struct dns_query_packet *dnsp;
	static union { struct sockaddr sa; struct sockaddr_in in0; } dns;

	dnsp = (struct dns_query_packet *)buf;
	flags = ntohs(dnsp->q_flags);

	if (flags == 0x100 &&
			dnsp->q_qdcount == htons(1)) {
		int error;
		char bufout[8192];

		queryp = (char *)(dnsp + 1);
		finishp = buf + count;
		dnscls = type = 0;
		queryp = dns_extract_name(name, sizeof(name), queryp, finishp);
		queryp = dns_extract_value(&type, sizeof(type), queryp, finishp);
		queryp = dns_extract_value(&dnscls, sizeof(dnscls), queryp, finishp);
		TX_PRINT(TXL_DEBUG, "query name: %s, type %d, class %d\n", name, htons(type), htons(dnscls));

		if (htons(0x1) == type && htons(0x1) == dnscls
				&& in_translate_blacklist() && is_fakedn(name)) {
			struct dns_query_packet *dnsoutp;
			struct sockaddr *so_addr1 = (struct sockaddr *)in_addr1;
			dnsoutp = (struct dns_query_packet *)bufout;
			error = get_cached_query(name, type, dnscls, bufout, sizeof(bufout));
			if (error > 0) {
				dnsoutp->q_ident = dnsp->q_ident;
				error = sendto(up->sockfd, bufout, error, 0, so_addr1, namlen);
				TX_PRINT(TXL_DEBUG, "get_cached_query return length: %d\n", error);
			}
			return 0;
		}

	} else if ((flags & 0x8000) && dnsp->q_ancount > htons(0)) {
		int error;
		int dnsttl = 0;
		int qcount = 0;
		int anscount = 0;
		int strip_fucking = 0;
		char valout[8192];
		unsigned short dnslen = 0;

		queryp = (char *)(dnsp + 1);
		finishp = buf + count;
		dnscls = type = 0;

		qcount = htons(dnsp->q_qdcount);
		for (int i = 0; i < qcount; i++) {
			queryp = dns_extract_name(name, sizeof(name), queryp, finishp);
			queryp = dns_extract_value(&type, sizeof(type), queryp, finishp);
			queryp = dns_extract_value(&dnscls, sizeof(dnscls), queryp, finishp);
			TX_PRINT(TXL_DEBUG, "query name: %s, type %d, class %d\n", name, htons(type), htons(dnscls));
		}

		anscount = htons(dnsp->q_ancount);
		strip_fucking = queryp - buf;
		for (int i = 0; i < anscount; i++) {
			queryp = dns_extract_name(name, sizeof(name), queryp, finishp);
			queryp = dns_extract_value(&type, sizeof(type), queryp, finishp);
			queryp = dns_extract_value(&dnscls, sizeof(dnscls), queryp, finishp);

			queryp = dns_extract_value(&dnsttl, sizeof(dnsttl), queryp, finishp);
			queryp = dns_extract_value(&dnslen, sizeof(dnslen), queryp, finishp);

			dnslen = htons(dnslen);
			queryp = dns_extract_value(valout, dnslen, queryp, finishp);

			if (dnscls != htons(1) || 
					type != htons(1) || dnslen != 4) {
				/* oh, we only check ipv4 address */
				continue;
			}

			if (is_fuckingip(valout)) {
				TX_PRINT(TXL_DEBUG, "fucking dns\n");
				dnsp->q_nscount = ntohs(0);
				dnsp->q_arcount = ntohs(0);
				dnsp->q_ancount = ntohs(0);
				count = strip_fucking;
				break;
			}

			if (in_translate_blacklist() &&
					(is_localip(valout) || is_localdn(name))) {
				/* do not change anything, since this is local net/dn */
				continue;
			}

			if (in_translate_whitelist() &&
					!(is_fakeip(valout) || is_fakedn(name))) {
				/* do not change anything, since this is not remote net/dn */
				continue;
			}

			/* start translate domain name */
			TX_PRINT(TXL_DEBUG, "forward name %s\n", name);
			unsigned int d_dest = get_wrap_ip(name);
			memcpy((char *)(queryp - 4), &d_dest, 4);
		}

	}

	if (flags & 0x8000) {
		/* from dns server */;
		int ident = htons(dnsp->q_ident);
		int index = (ident & 0x1FF);

		client = &__cached_client[index];
		if (client->flags == 1 &&
				client->r_ident == ident) {
			client->flags = 0;
			dnsp->q_ident = htons(client->l_ident);
			err = sendto(up->sockfd, buf, count, 0, &client->from.sa, sizeof(client->from));
			TX_PRINT(TXL_DEBUG, "sendto client %d/%d\n", err, errno);
		}

	} else {
		/* from dns client */;
		int index = (__last_index++ & 0x1FF);
		client = &__cached_client[index];
		memcpy(&client->from, in_addr1, namlen);
		client->flags = 1;
		client->l_ident = htons(dnsp->q_ident);
		client->r_ident = (rand() & 0xFE00) | index;
		dnsp->q_ident = htons(client->r_ident);

		dns.in0.sin_port = up->forward.port;
		dns.in0.sin_addr.s_addr = up->forward.address;
		err = sendto(up->outfd, buf, count, 0, &dns.sa, sizeof(dns.sa));
		TX_PRINT(TXL_DEBUG, "sendto server %d/%d\n", err, errno);
	}

	return 0;
}

static void do_dns_udp_recv(void *upp)
{
	int count;
	socklen_t in_len1;
	char buf[2048], s_buf[2048];
	struct sockaddr_in in_addr1;
	dns_udp_context_t *up = (dns_udp_context_t *)upp;

	while (tx_readable(&up->file)) {
		in_len1 = sizeof(in_addr1);
		count = recvfrom(up->sockfd, buf, sizeof(buf), 0,
				(struct sockaddr *)&in_addr1, &in_len1);
		tx_aincb_update(&up->file, count);
		if (count < 12) {
			TX_PRINT(TXL_DEBUG, "recvfrom len %d, %d, strerr %s", count, errno, strerror(errno));
			break;
		}

		dns_forward(up, buf, count, &in_addr1, in_len1);
	}

	while (tx_readable(&up->outgoing)) {
		in_len1 = sizeof(in_addr1);
		count = recvfrom(up->outfd, buf, sizeof(buf), 0,
				(struct sockaddr *)&in_addr1, &in_len1);
		tx_aincb_update(&up->outgoing, count);
		if (count < 12) {
			TX_PRINT(TXL_DEBUG, "recvfrom len %d, %d, strerr %s", count, errno, strerror(errno));
			break;
		}

		dns_forward(up, buf, count, &in_addr1, in_len1);
	}

	tx_aincb_active(&up->outgoing, &up->task);
	tx_aincb_active(&up->file, &up->task);
	return ;
}

int txdns_create(struct tcpip_info *local, struct tcpip_info *remote)
{
	int error;
	int outfd;
	int sockfd;
	int rcvbufsiz = 8192;
	tx_loop_t *loop;
	struct sockaddr_in in_addr1;
	dns_udp_context_t *up = NULL;

	sockfd = socket(AF_INET, SOCK_DGRAM, 0);
	TX_CHECK(sockfd != -1, "create dns socket failure");

	tx_setblockopt(sockfd, 0);
	setsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, (char *)&rcvbufsiz, sizeof(rcvbufsiz));

	in_addr1.sin_family = AF_INET;
	in_addr1.sin_port = local->port;
	in_addr1.sin_addr.s_addr = local->address;
	error = bind(sockfd, (struct sockaddr *)&in_addr1, sizeof(in_addr1));
	TX_CHECK(error == 0, "bind dns socket failure");

	outfd = socket(AF_INET, SOCK_DGRAM, 0);
	TX_CHECK(outfd != -1, "create dns out socket failure");

	tx_setblockopt(outfd, 0);
	setsockopt(outfd, SOL_SOCKET, SO_RCVBUF, (char *)&rcvbufsiz, sizeof(rcvbufsiz));

	in_addr1.sin_family = AF_INET;
	in_addr1.sin_port = 0;
	in_addr1.sin_addr.s_addr = 0;
	error = bind(outfd, (struct sockaddr *)&in_addr1, sizeof(in_addr1));
	TX_CHECK(error == 0, "bind dns out socket failure");

	up = new dns_udp_context_t();
	loop = tx_loop_default();

	up->forward = *remote;
	up->outfd = outfd;
	tx_aiocb_init(&up->outgoing, loop, outfd);

	up->sockfd = sockfd;
	tx_aiocb_init(&up->file, loop, sockfd);
	tx_task_init(&up->task, loop, do_dns_udp_recv, up);

	tx_aincb_active(&up->file, &up->task);
	tx_aincb_active(&up->outgoing, &up->task);

	return 0;
}
