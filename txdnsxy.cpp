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
#define SUFFIXES ".n.yiz.me"

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
		const char * dnsp, const char * finp, const char *packet, const char *limit)
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
			if (packet != NULL && packet + offset < limit) {
				dns_extract_name(name, namlen, packet + offset, limit, packet, limit);
				lastdot = &nouse;
			} else
			fprintf(stderr, "keywork %d %p %p\n", offset, packet + offset, finp);
			break;
		}

		if (dnsp + partlen > finp) {
			fprintf(stderr, "dns failure: %x/%s %lx\n", partlen, savp, finp - sdnsp);
			*lastdot = 0;
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

		if (dnsp == finp) {
			printf("failure extract\n");
			break;
		}
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
			name++;
			assert(count < 64);
			if (count > 0) {
				*lastdot = count;
				lastdot = outp++;
			}
			count = 0;
			continue;
		}

		*outp++ = *name++;
		count++;
	}

	*outp = 0;
	*lastdot = count;
	if (count > 0) outp++;
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
		return NULL;
	}

	ln = strlen(n);
	if (lt < ln && strcmp(n + ln - lt, tail) == 0) {
		n[ln - lt] = 0;
		return name;
	}

	return NULL;
}

char * dns_copy_cname(char *outp, const char * name)
{
	unsigned short dnslen = strlen(name);
	char *d, *plen, *mark;

	plen = outp;
	outp = dns_copy_value(outp, &dnslen, sizeof(dnslen));
	mark = outp;

	outp = dns_copy_name(outp, name);

	dnslen = htons(outp - mark);
	dns_copy_value(plen, &dnslen, sizeof(dnslen));

	return outp;
}

static char __rr_name[128];
static char __rr_desc[128];

char * dns_convert_value(int type, char *outp, char * valp, size_t count, char *packet, const char *limit, int trace)
{
	unsigned short dnslen = htons(count);
	char *d, n[256] = "", *plen, *mark;

	__rr_name[0] = 0;
	if (htons(type) == 0x05 || htons(type) == 0x02) { //CNAME
		plen = outp;
		outp = dns_copy_value(outp, &dnslen, sizeof(dnslen));
		mark = outp;

		d = (char *)dns_extract_name(n, sizeof(n), valp, valp + count, packet, limit);
		if (htons(type) == 0x05 && trace) {
			strcpy(__rr_name, n);
			strcat(n, SUFFIXES);
		}
		snprintf(__rr_desc, sizeof(__rr_desc), "%s", n);

		outp = dns_copy_name(outp, n);
		outp = dns_copy_value(outp, d, valp + count - d);

		dnslen = htons(outp - mark);
		dns_copy_value(plen, &dnslen, sizeof(dnslen));

	} else if (htons(type) == 0x06) {
		plen = outp;
		outp = dns_copy_value(outp, &dnslen, sizeof(dnslen));
		mark = outp;

		d = (char *)dns_extract_name(n, sizeof(n), valp, valp + count, packet, limit);
		snprintf(__rr_desc, sizeof(__rr_desc), "%s ", n);
		outp = dns_copy_name(outp, n);

		d = (char *)dns_extract_name(n, sizeof(n), d, valp + count, packet, limit);
		strcat(__rr_desc, n);
		outp = dns_copy_name(outp, n);

		outp = dns_copy_value(outp, d, valp + count - d);

		dnslen = htons(outp - mark);
		dns_copy_value(plen, &dnslen, sizeof(dnslen));
	} else {
		/* XXX */
		snprintf(__rr_desc, sizeof(__rr_desc), "");
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
	int len_cached;
	char pair_cached[1400];

	union {
		struct sockaddr sa;
		struct sockaddr_in in0;
	} from;
} __cached_client[512];

static int __last_index = 0;

int add_domain(const char *name, unsigned int localip)
{
	return 1;
}

#if 1
static int _localip_ptr = 0;
static unsigned int _localip_matcher[20480];

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
static unsigned int _fakenet_matcher[20480];

int add_fakenet(unsigned int network, unsigned int mask)
{
	int index = _fakenet_ptr++;
	_fakenet_matcher[index++] = htonl(network);
	_fakenet_matcher[index] = ~mask;
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

struct dns_udp_context_t {
	int sockfd;
	tx_aiocb file;

	int outfd;
	tx_aiocb outgoing;
	struct tcpip_info forward;

	tx_task_t task;
};

const char *dns_type(int type)
{
	static char _unkown_type[128];
	sprintf(_unkown_type, "NST%x", type);
	switch(type) {
		case 28: return "AAAA";
		case 1: return "A";
		case 5: return "CNAME";
		case 2: return "SOA";
		case 6: return "SRV";
		case 41: return "OPT";
	}

	return _unkown_type;
}

int get_suffixes_forward(char *dnsdst, size_t dstlen, const char *dnssrc, size_t srclen)
{
	char name[512];
	char shname[256];
	unsigned short type = 0;
	unsigned short dnscls = 0;

	const char * src_buf;
	const char * src_limit;
	struct dns_query_packet *dns_srcp;

	char * dst_buf;
	char * dst_limit;
	struct dns_query_packet *dns_dstp;

	dns_srcp = (struct dns_query_packet *)dnssrc;
	src_buf  = (char *)(dns_srcp + 1);
	src_limit = (char *)(dnssrc + srclen);

	dns_dstp = (struct dns_query_packet *)dnsdst;
	dst_buf  = (char *)(dns_dstp + 1);
	dst_limit = (char *)(dnsdst + dstlen);

	fprintf(stderr, "nsflag %x\n", htons(dns_srcp->q_flags));
	dns_dstp[0] = dns_srcp[0];
	for (int i = 0; i < htons(dns_srcp->q_qdcount); i++) {
		strcpy(name, "");
		src_buf = dns_extract_name(name, sizeof(name), src_buf, src_limit, dnssrc, src_limit);
		src_buf = dns_extract_value(&type, sizeof(type), src_buf, src_limit);
		src_buf = dns_extract_value(&dnscls, sizeof(dnscls), src_buf, src_limit);

		if (!dns_strip_tail(name, SUFFIXES)) return 0;
		TX_PRINT(TXL_DEBUG, "query name: %s, type %d, class %d \n", name, htons(type), htons(dnscls));
		dst_buf = dns_copy_name(dst_buf, name);
		dst_buf = dns_copy_value(dst_buf, &type, sizeof(type));
		dst_buf = dns_copy_value(dst_buf, &dnscls, sizeof(dnscls));
	}

	int dnsttl = 0;
	char valout[8192];
	unsigned short dnslen = 0;

	int nrecord = htons(dns_srcp->q_ancount) + htons(dns_srcp->q_nscount) + htons(dns_srcp->q_arcount);
	for (int i = 0; i < nrecord; i++) {
		strcpy(name, "");
		src_buf = dns_extract_name(name, sizeof(name), src_buf, src_limit, dnssrc, src_limit);
		src_buf = dns_extract_value(&type, sizeof(type), src_buf, src_limit);
		src_buf = dns_extract_value(&dnscls, sizeof(dnscls), src_buf, src_limit);
		src_buf = dns_extract_value(&dnsttl, sizeof(dnsttl), src_buf, src_limit);
		src_buf = dns_extract_value(&dnslen, sizeof(dnslen), src_buf, src_limit);
		src_buf = dns_extract_value(valout, htons(dnslen), src_buf, src_limit);

		dns_strip_tail(name, SUFFIXES);
		dst_buf = dns_copy_name(dst_buf, name);
		dst_buf = dns_copy_value(dst_buf, &type, sizeof(type));
		dst_buf = dns_copy_value(dst_buf, &dnscls, sizeof(dnscls));
		dst_buf = dns_copy_value(dst_buf, &dnsttl, sizeof(dnsttl));
		dst_buf = dns_convert_value(type, dst_buf, valout, htons(dnslen), (char *)dnssrc, src_limit, 0);
	}

	return (dst_buf - dnsdst);
}

int in_list(const char *list, const char *name)
{
	int test = 0;
	const char *ptr, *np;

	np = name;
	for (ptr = list; *ptr || ptr[1]; ptr++) {
		if (*np == *ptr && test == 0) {
			np++;
		} else {
			test = 1;
		}

		if (test == 0 && *np == 0) {
			break;
		}

		if (*ptr == 0) {
			np = name;
			test = 0;
		}
	}

	return test == 0 && *np == 0;
}

int get_suffixes_backward(char *dnsdst, size_t dstlen, const char *dnssrc, size_t srclen)
{
	char name[512];
	char shname[256];
	unsigned short type = 0;
	unsigned short dnscls = 0;

	const char * src_buf;
	const char * src_limit;
	struct dns_query_packet *dns_srcp;

	char * dst_buf;
	char * dst_limit;
	struct dns_query_packet *dns_dstp;

	dns_srcp = (struct dns_query_packet *)dnssrc;
	src_buf  = (char *)(dns_srcp + 1);
	src_limit = (char *)(dnssrc + srclen);

	dns_dstp = (struct dns_query_packet *)dnsdst;
	dst_buf  = (char *)(dns_dstp + 1);
	dst_limit = (char *)(dnsdst + dstlen);

	int have_cname = 0;
	char  wrap_name_list[1080];
	char *wrap = wrap_name_list;

	dns_dstp[0] = dns_srcp[0];
	for (int i = 0; i < htons(dns_srcp->q_qdcount); i++) {
		strcpy(name, "");
		src_buf = dns_extract_name(name, sizeof(name), src_buf, src_limit, dnssrc, src_limit);
		src_buf = dns_extract_value(&type, sizeof(type), src_buf, src_limit);
		src_buf = dns_extract_value(&dnscls, sizeof(dnscls), src_buf, src_limit);

		wrap += sprintf(wrap, "%s", name);
		wrap++; *wrap = 0;
		if (htons(type) == 0x01) {
			have_cname = 1;
		}

		snprintf(shname, sizeof(shname), "%s"SUFFIXES, name);
		TX_PRINT(TXL_DEBUG, "query name: %s, type %d, class %d \n", shname, htons(type), htons(dnscls));
		dst_buf = dns_copy_name(dst_buf, shname);
		dst_buf = dns_copy_value(dst_buf, &type, sizeof(type));
		dst_buf = dns_copy_value(dst_buf, &dnscls, sizeof(dnscls));
	}

	int dnsttl = 0;
	int trace_cname = 0;
	char valout[8192];
	const char * buf_mark = src_buf;
	unsigned short dnslen = 0;

	int nrecord = htons(dns_srcp->q_ancount) + htons(dns_srcp->q_nscount) + htons(dns_srcp->q_arcount);
	for (int i = 0; i < nrecord; i++) {
		src_buf = dns_extract_name(name, sizeof(name), src_buf, src_limit, dnssrc, src_limit);
		src_buf = dns_extract_value(&type, sizeof(type), src_buf, src_limit);
		src_buf = dns_extract_value(&dnscls, sizeof(dnscls), src_buf, src_limit);
		src_buf = dns_extract_value(&dnsttl, sizeof(dnsttl), src_buf, src_limit);
		src_buf = dns_extract_value(&dnslen, sizeof(dnslen), src_buf, src_limit);
		src_buf = dns_extract_value(valout, htons(dnslen), src_buf, src_limit);

		if (htons(type) == 0x01 && is_fakedn(name)) {
			trace_cname = 1;
			break;
		}
	}

	src_buf = buf_mark;
	int newcount = htons(dns_srcp->q_ancount);
	for (int i = 0; i < nrecord; i++) {
		strcpy(name, "");
		src_buf = dns_extract_name(name, sizeof(name), src_buf, src_limit, dnssrc, src_limit);
		src_buf = dns_extract_value(&type, sizeof(type), src_buf, src_limit);
		src_buf = dns_extract_value(&dnscls, sizeof(dnscls), src_buf, src_limit);
		src_buf = dns_extract_value(&dnsttl, sizeof(dnsttl), src_buf, src_limit);
		src_buf = dns_extract_value(&dnslen, sizeof(dnslen), src_buf, src_limit);
		src_buf = dns_extract_value(valout, htons(dnslen), src_buf, src_limit);

		if (have_cname && !trace_cname) {
			char cname[128];
			unsigned short int test_type = htons(5);
			sprintf(cname, "%s%s", wrap_name_list, SUFFIXES);
			dst_buf = dns_copy_name(dst_buf, cname);
			dst_buf = dns_copy_value(dst_buf, &test_type, sizeof(test_type));
			dst_buf = dns_copy_value(dst_buf, &dnscls, sizeof(dnscls));
			dst_buf = dns_copy_value(dst_buf, &dnsttl, sizeof(dnsttl));
			dst_buf = dns_copy_cname(dst_buf, wrap_name_list);
			have_cname = 0;
			newcount++;
		} else if (in_list(wrap_name_list, name)) {
			strcat(name, SUFFIXES);
		}

		dst_buf = dns_copy_name(dst_buf, name);
		dst_buf = dns_copy_value(dst_buf, &type, sizeof(type));
		dst_buf = dns_copy_value(dst_buf, &dnscls, sizeof(dnscls));
		dst_buf = dns_copy_value(dst_buf, &dnsttl, sizeof(dnsttl));
		dst_buf = dns_convert_value(type, dst_buf, valout, htons(dnslen), (char *)dnssrc, src_limit, trace_cname);
		fprintf(stderr, "rr %s %s %s\n", dns_type(htons(type)), name, __rr_desc);

		if (__rr_name[0] && trace_cname ) {
			wrap += sprintf(wrap, "%s", __rr_name);
			wrap++; *wrap = 0;
		}
	}

	dns_dstp->q_ancount = htons(newcount);

	return (dst_buf - dnsdst);
}

int dns_forward(dns_udp_context_t *up, char *buf, size_t count, struct sockaddr_in *in_addr1, socklen_t namlen, int fakeresp)
{
	int err;
	int len;
	int nsflag;
	char bufout[8192];

	struct cached_client *client;
	struct dns_query_packet *dnsp, *dnsoutp;
	static union { struct sockaddr sa; struct sockaddr_in in0; } dns;

	dnsp = (struct dns_query_packet *)buf;
	dnsoutp = (struct dns_query_packet *)bufout;

	nsflag = ntohs(dnsp->q_flags);
	if (nsflag & 0x8000) {
		int ident = htons(dnsp->q_ident);
		client = &__cached_client[ident & 0x1FF];
		if (client->r_ident != ident) {
			TX_PRINT(TXL_DEBUG, "get unexpected response, just return\n");
			return 0;
		}
		len = get_suffixes_backward(bufout, sizeof(bufout), buf, count);
		dnsoutp->q_ident = htons(client->l_ident);

		len > 0 && (err = sendto(up->sockfd, bufout, len, 0, &client->from.sa, sizeof(client->from)));
		TX_PRINT(TXL_DEBUG, "sendto client %d/%d, %x %d\n", err, errno, client->flags, ident);
	} else {
		int index = (__last_index++ & 0x1FF);
		client = &__cached_client[index];
		memcpy(&client->from, in_addr1, namlen);
		client->l_ident = htons(dnsp->q_ident);
		client->r_ident = (rand() & 0xFE00) | index;
		client->len_cached = 0;
		len = get_suffixes_forward(bufout, sizeof(bufout), buf, count);
		dnsoutp->q_flags |= htons(0x100);
		dnsoutp->q_ident  = htons(client->r_ident);

		dns.in0.sin_family = AF_INET;
		dns.in0.sin_port = up->forward.port;
		dns.in0.sin_addr.s_addr = up->forward.address;
		len > 0 && (err = sendto(up->outfd, bufout, len, 0, &dns.sa, sizeof(dns.sa)));
		TX_PRINT(TXL_DEBUG, "sendto server %d/%d, %x %d\n", err, errno, client->flags, index);
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

#if defined(SO_MARK)
	int mark = 0x3cc3;
	error = setsockopt(outfd, SOL_SOCKET, SO_MARK, &mark, sizeof(mark));
	TX_CHECK(error == 0, "set udp dns socket mark failure");
#endif

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

