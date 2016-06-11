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
//#define SUFFIXES ".n.yiz.me"
#define SUFFIXES ""

#define DNSFMT_ASSERT(expr, msgfmt) do { if (expr); else { printf msgfmt; abort(); } } while ( 0 )

struct dns_query_packet {
	unsigned short q_ident;
	unsigned short q_flags;
	unsigned short q_qdcount;
	unsigned short q_ancount;
	unsigned short q_nscount;
	unsigned short q_arcount;
};

struct dns_decode_packet {
	int err;
	struct dns_query_packet head;

	u_char *base;
	u_char *limit;
	u_char *cursor;
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

const u_char * dns_extract_name(struct dns_decode_packet *dpt, const void *finish, void *name, size_t namlen)
{

	int potlen;
	u_char *cursor = dpt->cursor;
	u_char *limit  = (u_char *)finish;

	char dot = '.';
	char *lastdot = &dot;

	char *namptr = (char *)name;
	char *namlimit = (char *)(namptr + namlen);

	DNSFMT_ASSERT(cursor < limit, ("dns format error L%d\n", __LINE__));

	potlen = *cursor++;
	while (potlen > 0) {
		unsigned short offset = 0;

		if (potlen & 0xc0) {
			offset = (potlen & 0x3F) << 8;
			DNSFMT_ASSERT(cursor < limit, ("dns format error L%d\n", __LINE__));
			dpt->cursor = dpt->base + offset + *cursor++;
			DNSFMT_ASSERT(namptr < namlimit, ("no enghout buffer L%d\n", __LINE__));
			dns_extract_name(dpt, dpt->limit, namptr, namlimit - namptr);
			dpt->cursor = cursor;
			lastdot = &dot;
			break;
		} else {
			DNSFMT_ASSERT(cursor + potlen <= limit, ("dns format error L%d %p %d %p %p\n", __LINE__, cursor, potlen, limit, dpt->base));
			DNSFMT_ASSERT(namptr + potlen < namlimit, ("no enghout buffer L%d\n", __LINE__));
			memcpy(namptr, cursor, potlen); 
			cursor += potlen;
			namptr += potlen;

			lastdot   = namptr;
			*namptr++ = '.';
		}

		potlen = *cursor++;
	}

	dpt->cursor = cursor;
	*lastdot = 0;
	return cursor;
}

const u_char * dns_extract_value(struct dns_decode_packet *dpt, const void *finish, void *valp, size_t size)
{
	u_char *cursor = dpt->cursor;
	DNSFMT_ASSERT(cursor + size <= finish, ("dns format error L%d\n", __LINE__));

	memcpy(valp, cursor, size);
	dpt->cursor += size;
	cursor += size;

	return cursor;
}

u_char * dns_copy_name(u_char *outp, const char * name)
{
	int count = 0;
	u_char * lastdot = outp++;

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

u_char * dns_copy_value(u_char *outp, void * valp, size_t count)
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

u_char * dns_copy_cname(u_char *outp, const char * name)
{
	unsigned short dnslen = strlen(name);
	u_char *d, *plen, *mark;

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

u_char * dns_convert_value(struct dns_decode_packet *pkt, size_t count, int trace, int type, u_char *outp, const char *detail)
{
	char name[256], alias[256];
	unsigned short prival = 0;
	u_char *d, *plen, *mark, *limit = pkt->cursor + count;
	unsigned short dnslen = htons(count);

#define NSTYPE_A     1
#define NSTYPE_NS    2
#define NSTYPE_CNAME 5
#define NSTYPE_SOA   6
#define NSTYPE_MX   15
#define NSTYPE_AAAA 28

	__rr_name[0] = 0;
	if (htons(type) == NSTYPE_CNAME || htons(type) == NSTYPE_NS) { //CNAME
		plen = outp;
		outp = dns_copy_value(outp, &dnslen, sizeof(dnslen));
		mark = outp;

		d = (u_char *)dns_extract_name(pkt, limit, name, sizeof(name));
		if (htons(type) == 0x05) {
			if (trace) {
				strcpy(__rr_name, name);
				strcat(name, SUFFIXES);
			} else {
				strcpy(__rr_name, name);
				dns_strip_tail(name, SUFFIXES);
			}

			if (strcmp(detail, name) == 0) {
				TX_PRINT(TXL_DEBUG, "ignore CNAME %s", detail);
				return plen;
			}
		}
		TX_PRINT(TXL_DEBUG, "%s: %s %s", htons(type)==0x05? "CNAME": "NS", detail, name);
		snprintf(__rr_desc, sizeof(__rr_desc), "%s", name);

		outp = dns_copy_name(outp, name);
		outp = dns_copy_value(outp, d, limit - d);

		dnslen = htons(outp - mark);
		dns_copy_value(plen, &dnslen, sizeof(dnslen));
	} else if (htons(type) == NSTYPE_MX) {
		plen = outp;
		outp = dns_copy_value(outp, &dnslen, sizeof(dnslen));
		mark = outp;

		dns_extract_value(pkt, limit, &prival, sizeof(prival));
		outp = dns_copy_value(outp, &prival, sizeof(prival));

		d = (u_char *)dns_extract_name(pkt, limit, name, sizeof(name));
		snprintf(__rr_desc, sizeof(__rr_desc), "%s ", name);
		outp = dns_copy_name(outp, name);

		outp = dns_copy_value(outp, d, limit - d);
		TX_PRINT(TXL_DEBUG, "MX %s %s %d", detail, name, htons(prival));

		dnslen = htons(outp - mark);
		dns_copy_value(plen, &dnslen, sizeof(dnslen));
	} else if (htons(type) == NSTYPE_SOA) {
		plen = outp;
		outp = dns_copy_value(outp, &dnslen, sizeof(dnslen));
		mark = outp;

		d = (u_char *)dns_extract_name(pkt, limit, name, sizeof(name));
		snprintf(__rr_desc, sizeof(__rr_desc), "%s ", name);
		outp = dns_copy_name(outp, name);

		strcpy(alias, name);
		d = (u_char *)dns_extract_name(pkt, limit, name, sizeof(name));
		strcat(__rr_desc, name);
		outp = dns_copy_name(outp, name);

		outp = dns_copy_value(outp, d, limit - d);
		TX_PRINT(TXL_DEBUG, "SOA %s %s %s", detail, alias, name);

		dnslen = htons(outp - mark);
		dns_copy_value(plen, &dnslen, sizeof(dnslen));
	} else {
		/* XXX */
		char nstype[64];
		sprintf(nstype, "NST%d", htons(type));
		htons(type) == NSTYPE_A && strcpy(nstype, "A");
		htons(type) == NSTYPE_AAAA && strcpy(nstype, "AAAA");
		TX_PRINT(TXL_DEBUG, "%s %s", nstype, detail);
		snprintf(__rr_desc, sizeof(__rr_desc), "");
		outp = dns_copy_value(outp, &dnslen, sizeof(dnslen));
		outp = dns_copy_value(outp, pkt->cursor, count);
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
	struct dns_query_packet *dns_srcp;
	struct dns_decode_packet  dns_pkt = {0};

	u_char * dst_buf;
	u_char * dst_limit;
	struct dns_query_packet *dns_dstp;

	dns_srcp = (struct dns_query_packet *)dnssrc;
	dns_pkt.base = (u_char *)dnssrc;
	dns_pkt.limit = (u_char *)(dnssrc + srclen);
	dns_pkt.cursor = (u_char *)(dns_srcp + 1);

	dns_dstp = (struct dns_query_packet *)dnsdst;
	dst_buf  = (u_char *)(dns_dstp + 1);
	dst_limit = (u_char *)(dnsdst + dstlen);

	fprintf(stderr, "nsflag %x\n", htons(dns_srcp->q_flags));
	dns_dstp[0] = dns_srcp[0];
	for (int i = 0; i < htons(dns_srcp->q_qdcount); i++) {
		strcpy(name, "");
		dns_extract_name(&dns_pkt, dns_pkt.limit, name, sizeof(name));
		dns_extract_value(&dns_pkt, dns_pkt.limit, &type, sizeof(type));
		dns_extract_value(&dns_pkt, dns_pkt.limit, &dnscls, sizeof(dnscls));

		if (!dns_strip_tail(name, SUFFIXES)) return 0;
		TX_PRINT(TXL_DEBUG, "forward suffixes name: %s, type %d, class %d \n", name, htons(type), htons(dnscls));
		dst_buf = dns_copy_name(dst_buf, name);
		dst_buf = dns_copy_value(dst_buf, &type, sizeof(type));
		dst_buf = dns_copy_value(dst_buf, &dnscls, sizeof(dnscls));
	}

	int anc = 0;
	int dnsttl = 0;
	u_char *marker0;
	u_char *marker1;
	u_char *markcursor;
	unsigned short dnslen = 0;

	int nrecord = htons(dns_srcp->q_ancount) + htons(dns_srcp->q_nscount) + htons(dns_srcp->q_arcount);
	for (int i = 0; i < nrecord; i++) {
		strcpy(name, "");
		dns_extract_name(&dns_pkt, dns_pkt.limit, name, sizeof(name));
		dns_extract_value(&dns_pkt, dns_pkt.limit, &type, sizeof(type));
		dns_extract_value(&dns_pkt, dns_pkt.limit, &dnscls, sizeof(dnscls));
		dns_extract_value(&dns_pkt, dns_pkt.limit, &dnsttl, sizeof(dnsttl));
		dns_extract_value(&dns_pkt, dns_pkt.limit, &dnslen, sizeof(dnslen));
		markcursor = dns_pkt.cursor + htons(dnslen);

		dns_strip_tail(name, SUFFIXES);
		marker0 = dst_buf;
		dst_buf = dns_copy_name(dst_buf, name);
		dst_buf = dns_copy_value(dst_buf, &type, sizeof(type));
		dst_buf = dns_copy_value(dst_buf, &dnscls, sizeof(dnscls));
		dst_buf = dns_copy_value(dst_buf, &dnsttl, sizeof(dnsttl));
		marker1 = dns_convert_value(&dns_pkt, htons(dnslen), 0, type, dst_buf, name);
		dst_buf = (marker1 != dst_buf? marker1: (anc++, marker0));
		dns_pkt.cursor = markcursor;
	}

	if (anc > 0 && anc < htons(dns_srcp->q_ancount)) {
		int count = htons(dns_srcp->q_ancount);
		dns_dstp->q_ancount = htons(count - anc);
	}

	return dst_buf - (u_char *)dnsdst;
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
	struct dns_query_packet *dns_srcp;
	struct dns_decode_packet  dns_pkt = {0};

	u_char * dst_buf;
	u_char * dst_limit;
	struct dns_query_packet *dns_dstp;

	dns_srcp = (struct dns_query_packet *)dnssrc;
	dns_pkt.base = (u_char *)dnssrc;
	dns_pkt.limit = (u_char *)(dnssrc + srclen);
	dns_pkt.cursor = (u_char *)(dns_srcp + 1);

	dns_dstp = (struct dns_query_packet *)dnsdst;
	dst_buf  = (u_char *)(dns_dstp + 1);
	dst_limit = (u_char *)(dnsdst + dstlen);

	fprintf(stderr, "nsflag %x\n", htons(dns_srcp->q_flags));

	char  wrap_name_list[1080];
	char *wrap = wrap_name_list;

	dns_dstp[0] = dns_srcp[0];
	for (int i = 0; i < htons(dns_srcp->q_qdcount); i++) {
		strcpy(name, "");
		dns_extract_name(&dns_pkt, dns_pkt.limit, name, sizeof(name));
		dns_extract_value(&dns_pkt, dns_pkt.limit, &type, sizeof(type));
		dns_extract_value(&dns_pkt, dns_pkt.limit, &dnscls, sizeof(dnscls));

		wrap += sprintf(wrap, "%s", name);
		wrap++; *wrap = 0;

		snprintf(shname, sizeof(shname), "%s"SUFFIXES, name);
		TX_PRINT(TXL_DEBUG, "backward suffixes name: %s, type %d, class %d \n", shname, htons(type), htons(dnscls));
		dst_buf = dns_copy_name(dst_buf, shname);
		dst_buf = dns_copy_value(dst_buf, &type, sizeof(type));
		dst_buf = dns_copy_value(dst_buf, &dnscls, sizeof(dnscls));
	}

	int dnsttl = 0;
	int trace_cname = 0;
	unsigned short dnslen = 0;
	u_char * cursor_mark = dns_pkt.cursor;

	int nrecord = htons(dns_srcp->q_ancount) + htons(dns_srcp->q_nscount) + htons(dns_srcp->q_arcount);
	for (int i = 0; i < nrecord; i++) {
		dns_extract_name(&dns_pkt, dns_pkt.limit, name, sizeof(name));
		dns_extract_value(&dns_pkt, dns_pkt.limit, &type, sizeof(type));
		dns_extract_value(&dns_pkt, dns_pkt.limit, &dnscls, sizeof(dnscls));
		dns_extract_value(&dns_pkt, dns_pkt.limit, &dnsttl, sizeof(dnsttl));
		dns_extract_value(&dns_pkt, dns_pkt.limit, &dnslen, sizeof(dnslen));
		dns_pkt.cursor += htons(dnslen);

		if (is_fakedn(name)) {
			trace_cname = 1;
			break;
		}
	}

	int newcount = htons(dns_srcp->q_ancount);
	dns_pkt.cursor = cursor_mark;

	if (!trace_cname) {
		char cname[128];
		unsigned short int test_type = htons(5);
		sprintf(cname, "%s%s", wrap_name_list, SUFFIXES);
		dst_buf = dns_copy_name(dst_buf, cname);
		dst_buf = dns_copy_value(dst_buf, &test_type, sizeof(test_type));
		dst_buf = dns_copy_value(dst_buf, &dnscls, sizeof(dnscls));
		dst_buf = dns_copy_value(dst_buf, &dnsttl, sizeof(dnsttl));
		dst_buf = dns_copy_cname(dst_buf, wrap_name_list);
		dns_dstp->q_ancount = htons(newcount + 1);
	}

	for (int i = 0; i < nrecord; i++) {
		strcpy(name, "");
		dns_extract_name(&dns_pkt, dns_pkt.limit, name, sizeof(name));
		dns_extract_value(&dns_pkt, dns_pkt.limit, &type, sizeof(type));
		dns_extract_value(&dns_pkt, dns_pkt.limit, &dnscls, sizeof(dnscls));
		dns_extract_value(&dns_pkt, dns_pkt.limit, &dnsttl, sizeof(dnsttl));
		dns_extract_value(&dns_pkt, dns_pkt.limit, &dnslen, sizeof(dnslen));
		cursor_mark = dns_pkt.cursor + htons(dnslen);

		if (trace_cname && in_list(wrap_name_list, name)) {
			strcat(name, SUFFIXES);
		}

		dst_buf = dns_copy_name(dst_buf, name);
		dst_buf = dns_copy_value(dst_buf, &type, sizeof(type));
		dst_buf = dns_copy_value(dst_buf, &dnscls, sizeof(dnscls));
		dst_buf = dns_copy_value(dst_buf, &dnsttl, sizeof(dnsttl));
		dst_buf = dns_convert_value(&dns_pkt, htons(dnslen), trace_cname, type, dst_buf, name);
		fprintf(stderr, "rr %s %s %s\n", dns_type(htons(type)), name, __rr_desc);
		dns_pkt.cursor = cursor_mark;

		if (__rr_name[0] && trace_cname ) {
			wrap += sprintf(wrap, "%s", __rr_name);
			wrap++; *wrap = 0;
		}
	}

	return dst_buf - (u_char *)dnsdst;
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
		len = get_suffixes_forward(bufout, sizeof(bufout), buf, count);
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
		len = get_suffixes_backward(bufout, sizeof(bufout), buf, count);
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

