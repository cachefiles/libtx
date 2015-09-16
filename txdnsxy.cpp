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
		const char * dnsp, const char * finp, char *packet)
{
	int partlen;
	char nouse = '.';
	char * lastdot = &nouse;

	char *savp = name;
	const char *sdnsp = dnsp;
	if (dnsp == finp)
		return finp;

	/* int first = 1; */
	partlen = (unsigned char)*dnsp++;
	while (partlen) {
		unsigned short offset = 0;

		if (partlen & 0xC0) {
			offset = ((partlen & 0x3F) << 8);
			offset = (offset | (unsigned char )*dnsp++);
			if (packet != NULL) {
				/* if (first == 0) { *name++ = '.'; namlen--; } */
				dns_extract_name(name, namlen, packet + offset, packet + offset + 64, packet);
				fprintf(stderr, "after %x offsetk %d limit %ld %s/\n", partlen, offset, finp - packet, savp);
				lastdot = &nouse;
			}
			break;
		}

		if (dnsp + partlen > finp) {
			fprintf(stderr, "dns failure: %x/%s %lx\n", partlen, savp, finp - sdnsp);
			return finp;
		}

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
		/* first = 0; */
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

char * dns_strip_tail(char *name, const char *tail)
{
	char *n = name;
	size_t ln = -1;
	size_t lt = strlen(tail);

	if (n == NULL || *n == 0) {
		return name;
	}

	ln = strlen(n);
	if (lt < ln && strcmp(n + ln - lt, tail) == 0) {
		n[ln - lt] = 0;
	}

	return name;
}

char * dns_convert_value(int type, char *outp, char * valp, size_t count, char *packet)
{
	unsigned short dnslen = htons(count);
	char *d, n[256] = "", *plen, *mark;

	if (htons(type) == 0x05 || htons(type) == 0x02) { //CNAME
		plen = outp;
		outp = dns_copy_value(outp, &dnslen, sizeof(dnslen));
		mark = outp;

		d = (char *)dns_extract_name(n, sizeof(n), valp, (char *)(valp + count), packet);
		dns_strip_tail(n, ".n.yiz.me");

		outp = dns_copy_name(outp, n);
		outp = dns_copy_value(outp, d, valp + count - d);

		dnslen = htons(outp - mark);
		dns_copy_value(plen, &dnslen, sizeof(dnslen));

	} else if (htons(type) == 0x06) {
		plen = outp;
		outp = dns_copy_value(outp, &dnslen, sizeof(dnslen));
		mark = outp;

		d = (char *)dns_extract_name(n, sizeof(n), valp, (char *)(valp + count), packet);
		dns_strip_tail(n, ".n.yiz.me");
		outp = dns_copy_name(outp, n);

		d = (char *)dns_extract_name(n, sizeof(n), d, (char *)(d + count), packet);
		dns_strip_tail(n, ".n.yiz.me");
		outp = dns_copy_name(outp, n);

		outp = dns_copy_value(outp, d, valp + count - d);

		dnslen = htons(outp - mark);
		dns_copy_value(plen, &dnslen, sizeof(dnslen));
	} else {
		/* XXX */
		outp = dns_copy_value(outp, &dnslen, sizeof(dnslen));
		outp = dns_copy_value(outp, valp, count);
	}

	return outp;
}

static struct cached_client {
	int flags;
	unsigned short r_ident;
	unsigned short l_ident;

	int pair;
	unsigned int ipv4addr;

	union {
		struct sockaddr sa;
		struct sockaddr_in in0;
	} from;
} __cached_client[512];

static int __last_index = 0;

static int __last_type = 0;
static int __last_query = 0;
static char __last_fqdn[128];

#define CCF_ATTACHED 0x80
#define CCF_PENDING  0x01
#define CCF_IPV4     0x10
#define CCF_IPV6     0x40

int set_dynamic_range(unsigned int ip0, unsigned int ip9)
{
	return 0;
}

int add_domain(const char *name, unsigned int localip)
{
	return 1;
}


#if 1
int add_localnet(unsigned int network, unsigned int netmask)
{
	return 0;
}

int add_localdn(const char *dn)
{
	return 0;
}

int add_fakeip(unsigned int ip)
{
	return 0;
}

int add_fakenet(unsigned int ip, unsigned int mask)
{
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

int set_fuckingip(unsigned int ip)
{
	return 0;
}

int set_translate(int mode)
{
	return 0;
}

static int dns_setquestion(const char *name, unsigned short dnstyp, unsigned short dnscls, char *buf, size_t len)
{
	char *outp = NULL;
	struct dns_query_packet *dnsoutp;

	dnsoutp = (struct dns_query_packet *)buf;
	dnsoutp->q_flags = ntohs(0x100);
	dnsoutp->q_qdcount = ntohs(1);
	dnsoutp->q_nscount = ntohs(0);
	dnsoutp->q_arcount = ntohs(0);
	dnsoutp->q_ancount = ntohs(0);

	outp = (char *)(dnsoutp + 1);
	outp = dns_copy_name(outp, name);
	outp = dns_copy_value(outp, &dnstyp, sizeof(dnstyp));
	outp = dns_copy_value(outp, &dnscls, sizeof(dnscls));

	return (outp - buf);
}

static int dns_setanswer(const char *name, unsigned int valip, unsigned short dnstyp, unsigned short dnscls, char *buf, size_t len)
{
	int anscount;
	char *outp = NULL;
	unsigned short d_len = 0;
	unsigned int   d_ttl = htonl(3600);
	struct dns_query_packet *dnsoutp;

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

	d_len = htons(sizeof(valip));
	TX_PRINT(TXL_DEBUG, "return new forward: %s\n", name);

	outp = dns_copy_value(outp, &d_ttl, sizeof(d_ttl));
	outp = dns_copy_value(outp, &d_len, sizeof(d_len));
	outp = dns_copy_value(outp, &valip, sizeof(valip));
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
	struct tcpip_info forward;

	int fakefd;
	tx_aiocb fakegoing;
	struct tcpip_info faketarget;

#ifdef _ENABLE_INET6_
	int outfd6;
	tx_aiocb outgoing6;
	struct sockaddr_in6 forward6;
#endif

    int fakedelay;
	tx_task_t task;
};

int dns_merge_query(int index, const char *name, u_short ident, int type, struct sockaddr_in *addr1, size_t namlen)
{
	struct cached_client *client;
	index = (index & 0x1FF);

	client = &__cached_client[index];
	if (client->flags & CCF_ATTACHED) {
		TX_PRINT(TXL_DEBUG, "attach failure %s/%d\n", name, index);
		return -1;
	}

	if ((client->flags & CCF_PENDING) && (type & client->flags)) {
		client->flags |= CCF_ATTACHED;
		memcpy(&client->from, addr1, namlen);
		client->l_ident = htons(ident);
		return 0;
	}

	TX_PRINT(TXL_DEBUG, "last attach failure %s/%d\n", name, index);
	return -1;
}

int dns_forward(dns_udp_context_t *up, char *buf, size_t count, struct sockaddr_in *in_addr1, socklen_t namlen, int fakeresp, int pair = -1)
{
	int err;
	int flags;
	int ipv6v4 = 0;

	char name[512];
	const char *queryp;
	const char *finishp;
	unsigned short type, dnscls;
	struct cached_client *client;
	struct dns_query_packet *dnsp;
	static union { struct sockaddr sa; struct sockaddr_in in0; } dns;

	dnsp = (struct dns_query_packet *)buf;
	flags = ntohs(dnsp->q_flags);

	if ((flags & 0x8000) == 0) {
		int error;
		char bufout[8192];
		queryp = (char *)(dnsp + 1);
		finishp = buf + count;

		char *outp = NULL;
		struct dns_query_packet *dnsoutp;
		dnsoutp = (struct dns_query_packet *)bufout;
		outp = (char *)(dnsoutp + 1);

		*dnsoutp = *dnsp;
		for (int i = 0; i < htons(dnsp->q_qdcount); i++) {
			dnscls = type = 0;
			queryp = dns_extract_name(name, sizeof(name), queryp, finishp, (char *)dnsp);
			queryp = dns_extract_value(&type, sizeof(type), queryp, finishp);
			queryp = dns_extract_value(&dnscls, sizeof(dnscls), queryp, finishp);
			TX_PRINT(TXL_DEBUG, "query name: %s, type %d, class %d\n", name, htons(type), htons(dnscls));

			switch (htons(type)) {
				case 28:
					ipv6v4 = CCF_IPV6;
					break;

				case 01:
					ipv6v4 = CCF_IPV4;
					break;

				default:
					ipv6v4 = 0;
					break;
			}

			if (pair == -1) {
				if (htons(dnsp->q_qdcount) == 1 && ipv6v4 != 0) {
					if (ipv6v4 != __last_type &&
							strcmp(name, __last_fqdn) == 0) {
						TX_PRINT(TXL_DEBUG, "merge old query: %s %d\n", name, htons(type));
						dns_merge_query(__last_query, name, dnsp->q_ident, ipv6v4, in_addr1, namlen);
						__last_type = ipv6v4;
						return 0;
					} else {
						TX_PRINT(TXL_DEBUG, "start new query: %s %d\n", name, htons(type));
						strcpy(__last_fqdn, name);
						__last_type = ipv6v4;
					}
				}

				if (is_fakedn(name) && (ipv6v4 & CCF_IPV4)) {
					dnsp->q_flags = 0x8080;
					error = sendto(up->sockfd, buf, count, 0, (struct sockaddr *)in_addr1, namlen);
					TX_PRINT(TXL_DEBUG, "fake response return length: %d\n", error);
					return 0;
				}
			} else {
				switch (ipv6v4) {
					case CCF_IPV4:
						ipv6v4 = CCF_IPV6;
						type = htons(28);
						break;

					case CCF_IPV6:
						ipv6v4 = CCF_IPV4;
						type = htons(1);
						break;

					default:
						TX_PRINT(TXL_DEBUG, "should be fatal\n");
						break;
				}
			}

			if ((ipv6v4 & CCF_IPV6) || (ipv6v4 && is_fakedn(name))) {
				TX_PRINT(TXL_DEBUG, "tailing .n.yiz.me to avoid gfw inject.\n");
				strcat(name, ".n.yiz.me");
			}

			outp = dns_copy_name(outp, name);
			outp = dns_copy_value(outp, &type, sizeof(type));
			outp = dns_copy_value(outp, &dnscls, sizeof(dnscls));
		}

		/* from dns client */;
		int index = (__last_index++ & 0x1FF);
		client = &__cached_client[index];
		memcpy(&client->from, in_addr1, namlen);
		client->flags = (pair == -1? CCF_ATTACHED: 0) | CCF_PENDING | ipv6v4;
		client->l_ident = htons(dnsp->q_ident);
		client->r_ident = (rand() & 0xFE00) | index;
		dnsoutp->q_ident = htons(client->r_ident);

		dns.in0.sin_family = AF_INET;
		dns.in0.sin_port = up->forward.port;
		dns.in0.sin_addr.s_addr = up->forward.address;
		err = sendto(up->outfd, bufout, outp - bufout, 0, &dns.sa, sizeof(dns.sa));
		TX_PRINT(TXL_DEBUG, "sendto server %d/%d, %x %d\n", err, errno, client->flags, index);

		client->pair = pair;
		if (pair == -1 && ipv6v4 && htons(dnsp->q_qdcount) == 1) {
			client->pair  = dns_forward(up, buf, count, in_addr1, namlen, fakeresp, index);
			TX_PRINT(TXL_DEBUG, "generate auto query nown");
			__last_query  = client->pair;
		}

		return index;

	} else if ((flags & 0x8000) != 0) {
		int dnsttl = 0;
		int qcount = 0;
		int anscount = 0;
		char valout[8192];
		unsigned short dnslen = 0;

		queryp = (char *)(dnsp + 1);
		finishp = buf + count;

		char bufout[8192];
		char *outp = NULL;
		struct dns_query_packet *dnsoutp;

		dnsoutp = (struct dns_query_packet *)bufout;
		outp = (char *)(dnsoutp + 1);

		*dnsoutp = *dnsp;
		qcount = htons(dnsp->q_qdcount);
		for (int i = 0; i < qcount; i++) {
			name[0] = 0;
			dnscls = type = 0;
			queryp = dns_extract_name(name, sizeof(name), queryp, finishp, (char *)dnsp);
			queryp = dns_extract_value(&type, sizeof(type), queryp, finishp);
			queryp = dns_extract_value(&dnscls, sizeof(dnscls), queryp, finishp);
			TX_PRINT(TXL_DEBUG, "before isfake %d query name: %s, type %d, class %d\n", fakeresp, name, htons(type), htons(dnscls));
			dns_strip_tail(name, ".n.yiz.me");
			TX_PRINT(TXL_DEBUG, "after isfake %d query name: %s, type %d, class %d\n", fakeresp, name, htons(type), htons(dnscls));
			outp = dns_copy_name(outp, name);
			outp = dns_copy_value(outp, &type, sizeof(type));
			outp = dns_copy_value(outp, &dnscls, sizeof(dnscls));
		}

		anscount = htons(dnsp->q_ancount) + htons(dnsp->q_nscount) + htons(dnsp->q_arcount);
		for (int i = 0; i < anscount; i++) {
			name[0] = 0;
			queryp = dns_extract_name(name, sizeof(name), queryp, finishp, (char *)dnsp);
			queryp = dns_extract_value(&type, sizeof(type), queryp, finishp);
			queryp = dns_extract_value(&dnscls, sizeof(dnscls), queryp, finishp);

			queryp = dns_extract_value(&dnsttl, sizeof(dnsttl), queryp, finishp);
			queryp = dns_extract_value(&dnslen, sizeof(dnslen), queryp, finishp);

			int dnslenx = htons(dnslen);
			queryp = dns_extract_value(valout, dnslenx, queryp, finishp);
			dns_strip_tail(name, ".n.yiz.me");
			fprintf(stderr, "after: %s\n", name);
			outp = dns_copy_name(outp, name);
			outp = dns_copy_value(outp, &type, sizeof(type));
			outp = dns_copy_value(outp, &dnscls, sizeof(dnscls));
			outp = dns_copy_value(outp, &dnsttl, sizeof(dnsttl));
			outp = dns_convert_value(type, outp, valout, dnslenx, (char *)dnsp);
		}

		/* from dns server */;
		int ident = htons(dnsp->q_ident);
		int index = (ident & 0x1FF);

		client = &__cached_client[index];
		TX_PRINT(TXL_DEBUG, "client %d/%d %d %x\n", err, errno, index, client->flags);
		if ((client->flags & CCF_PENDING)
				&& (client->flags & CCF_ATTACHED)
				&& (client->r_ident == ident)) {
			dnsoutp->q_ident = htons(client->l_ident);
			err = sendto(up->sockfd, bufout, outp - bufout, 0, &client->from.sa, sizeof(client->from));
			TX_PRINT(TXL_DEBUG, "sendto client %d/%d %d %x\n", err, errno, index, client->flags);
			client->flags = 0;
			return 0;
		}
	}

	return 0;
}

static void do_dns_udp_recv(void *upp)
{
	int count;
	socklen_t in_len1;
	char buf[2048];
	struct sockaddr_in in_addr1;
	dns_udp_context_t *up = (dns_udp_context_t *)upp;

	while (tx_readable(&up->file)) {
		in_len1 = sizeof(in_addr1);
		count = recvfrom(up->sockfd, buf, sizeof(buf), 0,
				(struct sockaddr *)&in_addr1, &in_len1);
		tx_aincb_update(&up->file, count);
		if (count < 12) {
			// TX_PRINT(TXL_DEBUG, "recvfrom len %d, %d, strerr %s", count, errno, strerror(errno));
			break;
		}

		dns_forward(up, buf, count, &in_addr1, in_len1, 0);
	}

	while (tx_readable(&up->outgoing)) {
		in_len1 = sizeof(in_addr1);
		count = recvfrom(up->outfd, buf, sizeof(buf), 0,
				(struct sockaddr *)&in_addr1, &in_len1);
		tx_aincb_update(&up->outgoing, count);
		if (count < 12) {
			// TX_PRINT(TXL_DEBUG, "recvfrom len %d, %d, strerr %s", count, errno, strerror(errno));
			break;
		}

		dns_forward(up, buf, count, &in_addr1, in_len1, 0);
	}

	while (tx_readable(&up->fakegoing)) {
		in_len1 = sizeof(in_addr1);
		count = recvfrom(up->fakefd, buf, sizeof(buf), 0,
				(struct sockaddr *)&in_addr1, &in_len1);
		tx_aincb_update(&up->fakegoing, count);
		if (count < 12) {
			// TX_PRINT(TXL_DEBUG, "recvfrom len %d, %d, strerr %s", count, errno, strerror(errno));
			break;
		}

		/* this is fake response */
		dns_forward(up, buf, count, &in_addr1, in_len1, 1);
	}

#ifdef _ENABLE_INET6_
	while (up->outfd6 != -1 && tx_readable(&up->outgoing6)) {
		in_len1 = sizeof(in_addr1);
		count = recvfrom(up->outfd6, buf, sizeof(buf), 0,
				(struct sockaddr *)&in_addr1, &in_len1);
		tx_aincb_update(&up->outgoing6, count);
		if (count < 12) {
			// TX_PRINT(TXL_DEBUG, "recvfrom len %d, %d, strerr %s", count, errno, strerror(errno));
			break;
		}

		dns_forward(up, buf, count, &in_addr1, in_len1, 0);
	}

	if (up->outfd6 != -1) {
		// TX_PRINT(TXL_DEBUG, "recvfrom len %d, %d, strerr %s", count, errno, strerror(errno));
		tx_aincb_active(&up->outgoing6, &up->task);
	}
#endif

	tx_aincb_active(&up->fakegoing, &up->task);
	tx_aincb_active(&up->outgoing, &up->task);
	tx_aincb_active(&up->file, &up->task);
	return ;
}

int txdns_create(struct tcpip_info *local, struct tcpip_info *remote, struct tcpip_info *fake, int delay)
{
	int error;
	int outfd;
	int sockfd;
	int fakefd;
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

	fakefd = socket(AF_INET, SOCK_DGRAM, 0);
	TX_CHECK(fakefd != -1, "create dns out socket failure");

	tx_setblockopt(fakefd, 0);
	setsockopt(fakefd, SOL_SOCKET, SO_RCVBUF, (char *)&rcvbufsiz, sizeof(rcvbufsiz));

	in_addr1.sin_family = AF_INET;
	in_addr1.sin_port = 0;
	in_addr1.sin_addr.s_addr = 0;
	error = bind(fakefd, (struct sockaddr *)&in_addr1, sizeof(in_addr1));
	TX_CHECK(error == 0, "bind dns out socket failure");

	up = new dns_udp_context_t();
	loop = tx_loop_default();
	up->fakedelay = delay;

	up->forward = *remote;
	up->outfd = outfd;
	tx_aiocb_init(&up->outgoing, loop, outfd);

	up->faketarget = *fake;
	up->fakefd = fakefd;
	tx_aiocb_init(&up->fakegoing, loop, fakefd);

	up->sockfd = sockfd;
	tx_aiocb_init(&up->file, loop, sockfd);
	tx_task_init(&up->task, loop, do_dns_udp_recv, up);

	tx_aincb_active(&up->file, &up->task);
	tx_aincb_active(&up->outgoing, &up->task);
	tx_aincb_active(&up->fakegoing, &up->task);

#ifdef _ENABLE_INET6_
	struct sockaddr_in6 in_addr6 = {0};
	int outfd6 = socket(AF_INET6, SOCK_DGRAM, 0);
	TX_CHECK(outfd6 != -1, "create dns out6 socket failure");

	tx_setblockopt(outfd6, 0);
	setsockopt(outfd6, SOL_SOCKET, SO_RCVBUF, (char *)&rcvbufsiz, sizeof(rcvbufsiz));

	in_addr6.sin6_family = AF_INET6;
	in_addr6.sin6_port = 0;
	error = bind(outfd6, (struct sockaddr *)&in_addr6, sizeof(in_addr6));
	TX_CHECK(error == 0, "bind dns out6 socket failure");

	memset(&up->forward6, 0, sizeof(up->forward6));
	up->forward6.sin6_family = AF_INET6;
	up->forward6.sin6_port = htons(53);
	inet_pton(AF_INET6, "2001:470:20::2", &up->forward6.sin6_addr);

	up->outfd6 = outfd6;
	tx_aiocb_init(&up->outgoing6, loop, up->outfd6);
	tx_aincb_active(&up->outgoing6, &up->task);
#endif

	return 0;
}

static int _anti_delay = 0;
static struct tcpip_info _anti_fakens = {0};

void txantigfw_set(struct tcpip_info *fakens , int delay)
{
    _anti_fakens = *fakens;
    _anti_delay = delay;
}

int txdns_create(struct tcpip_info *local, struct tcpip_info *remote)
{
    if (_anti_delay > 0 && _anti_delay < 400) {
        txdns_create(local, remote, &_anti_fakens, _anti_delay);
        return 0;
    }

    txdns_create(local, remote, &_anti_fakens, 0);
    return 0;
}
