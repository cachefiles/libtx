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
#include "txdnsxy.h"


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
	unsigned int ni_flag;
	unsigned int ni_local;
	unsigned int ni_rcvtime;
	struct named_item *ni_next;
};

#define NIF_FIXED    0x01
static int _wrap_ip_base = 0x7f000002;
static int _wrap_ip_limit = 0x80000000;

static int _next_local = 0;
static struct named_item *_named_list_h = NULL;

int set_dynamic_range(unsigned int ip0, unsigned int ip9)
{
	_wrap_ip_base = htonl(ip0);
	_wrap_ip_limit = htonl(ip9);
	return 0;
}

unsigned int get_wrap_ip(const char *name)
{
	char *p, cached[256];
	struct named_item *item = 0;

	for (item = _named_list_h; item; item = item->ni_next) {
		if (strcmp(name, item->ni_name) == 0) {
			if (item->ni_flag & NIF_FIXED)
				return htonl(item->ni_local);
			item->ni_rcvtime = tx_getticks();
			return htonl(item->ni_local + _wrap_ip_base);
		}
	}

	item = new named_item;
	strcpy(item->ni_name, name);
	item->ni_flag = 0;
	item->ni_local = _next_local++;
	item->ni_rcvtime = tx_getticks();
	item->ni_next = _named_list_h;
	_named_list_h = item;
	return htonl(item->ni_local + _wrap_ip_base);
}

int add_domain(const char *name, unsigned int localip)
{
	char *p, cached[256];
	struct named_item *item = 0;

	for (item = _named_list_h; item; item = item->ni_next) {
		if (strcmp(name, item->ni_name) == 0) {
			item->ni_rcvtime = -1;
			return (item->ni_local == htonl(localip));
		}
	}

	item = new named_item;
	strcpy(item->ni_name, name);
	item->ni_flag = NIF_FIXED;
	item->ni_local = htonl(localip);
	item->ni_rcvtime = tx_getticks();
	item->ni_next = _named_list_h;
	_named_list_h = item;
	return 1;
}

/* get cache name by addr */
const char *get_unwrap_name(unsigned int addr)
{
	struct named_item *item;
	unsigned int orig = htonl(addr);
	unsigned int local = (orig - _wrap_ip_base);

	for (item = _named_list_h; item; item = item->ni_next) {
		if (item->ni_flag & NIF_FIXED) {
			if (item->ni_local == orig) return item->ni_name;
		} else if (item->ni_local == local) {
			return item->ni_name;
		}
	}

	return NULL;
}

#if 1
static int _localip_ptr = 0;
static unsigned int _localip_matcher[1024];

int add_localnet(unsigned int network, unsigned int netmask)
{
	int index = _localip_ptr++;
	_localip_matcher[index++] = htonl(network);
	_localip_matcher[index] = ~netmask;
	_localip_ptr++;
	return 0;
}

static int is_localip(const void *valout)
{
	int i;
	unsigned int ip;

	memcpy(&ip, valout, 4);
	ip = htonl(ip);

	for (i = 0; i < _localip_ptr; i += 2) {
		if (_localip_matcher[i] == (ip & _localip_matcher[i + 1])) {
			return 1;
		} 
	}

	return 0;
}

static int _localdn_ptr = 0;
static char _localdn_matcher[8192];

int add_localdn(const char *dn)
{
	char *ptr, *optr;
	const char *p = dn + strlen(dn);

	ptr = &_localdn_matcher[_localdn_ptr];

	optr = ptr;
	while (p-- > dn) {
		*++ptr = *p;
		_localdn_ptr++;
	}

	if (optr != ptr) {
		*optr = (ptr - optr);
		_localdn_ptr ++;
		*++ptr = 0;
	}

	return 0;
}

static int is_localdn(const char *name)
{
	int i, len;
	char *ptr, cache[256];
	const char *p = name + strlen(name);

	ptr = cache;
	assert((p - name) < sizeof(cache));

	while (p-- > name) {
		*ptr++ = *p;
	}
	*ptr++ = '.';
	*ptr = 0;

	ptr = cache;
	for (i = 0; i < _localdn_ptr; ) {
		len = (_localdn_matcher[i++] & 0xff);

		assert(len > 0);
		if (strncmp(_localdn_matcher + i, cache, len) == 0) {
			return 1;
		}

		i += len;
	}

	return 0;
}

static int _fakeip_ptr = 0;
static unsigned int _fakeip_matcher[1024];

int add_fakeip(unsigned int ip)
{
	int index = _fakeip_ptr++;
	_fakeip_matcher[index] = htonl(ip);
	return 0;
}

static int _fakenet_ptr = 0;
static unsigned int _fakenet_matcher[1024];

int add_fakenet(unsigned int network, unsigned int netmask)
{
	int index = _fakenet_ptr++;
	_fakenet_matcher[index++] = htonl(network);
	_fakenet_matcher[index] = ~netmask;
	_fakenet_ptr++;
	return 0;
}

static int is_fakeip(const void *valout)
{
	int i;
	unsigned int ip;

	memcpy(&ip, valout, 4);
	ip = htonl(ip);

	for (i = 0; i < _fakenet_ptr; i += 2) {
		if (_fakenet_matcher[i] == (ip & _fakenet_matcher[i + 1])) {
			return 1;
		} 
	}

	for (i = 0; i < _fakeip_ptr; i++) {
		if (_fakeip_matcher[i] == ip) {
			return 1;
		}
	}

	return 0;
}

static int _fakedn_ptr = 0;
static char _fakedn_matcher[8192];

int add_fakedn(const char *dn)
{
	char *ptr, *optr;
	const char *p = dn + strlen(dn);

	ptr = &_fakedn_matcher[_fakedn_ptr];

	optr = ptr;
	while (p-- > dn) {
		*++ptr = *p;
		_fakedn_ptr++;
	}

	if (optr != ptr) {
		*optr = (ptr - optr);
		_fakedn_ptr++;
		*++ptr = 0;
	}

	return 0;
}

static int is_fakedn(const char *name)
{
	int i, len;
	char *ptr, cache[256];
	const char *p = name + strlen(name);

	ptr = cache;
	assert((p - name) < sizeof(cache));

	while (p-- > name) {
		*ptr++ = *p;
	}
	*ptr++ = '.';
	*ptr = 0;

	ptr = cache;
	for (i = 0; i < _fakedn_ptr; ) {
		len = (_fakedn_matcher[i++] & 0xff);

		assert(len > 0);
		if (strncmp(_fakedn_matcher + i, cache, len) == 0) {
			return 1;
		}

		i += len;
	}

	return 0;
}

#endif

unsigned char fucking_dns[] = {0xdc, 0xfa, 0x40, 0xe4};

int set_fuckingip(unsigned int ip)
{
	memcpy(fucking_dns, &ip, 4);
	return 0;
}

static int is_fuckingip(void *valout)
{
	return memcmp(fucking_dns, valout, 4) == 0;
}

static int _translate_list = 0x0;

int set_translate(int mode)
{
	_translate_list = mode;
	return 0;
}

static int in_translate_whitelist()
{
	return (_translate_list == TRANSLATE_WHITELIST);
}

static int in_translate_blacklist()
{
	return (_translate_list == TRANSLATE_BLACKLIST);
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

			if (in_translate_whitelist() &&
					(is_localip(valout) || is_localdn(name))) {
				/* do not change anything, since this is local net/dn */
				continue;
			}

			if (in_translate_blacklist() &&
					!(is_fakeip(valout) || is_fakedn(name))) {
				/* do not change anything, since this is not remote net/dn */
				continue;
			}

			/* start translate domain name */
			if (in_translate_whitelist() || in_translate_blacklist()) {
				TX_PRINT(TXL_DEBUG, "forward name %s %d %d\n", name, in_translate_whitelist(), in_translate_blacklist());
				unsigned int d_dest = get_wrap_ip(name);
				memcpy((char *)(queryp - 4), &d_dest, 4);
			}
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

		dns.in0.sin_family = AF_INET;
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
