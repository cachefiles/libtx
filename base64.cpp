#include <stdio.h>
#include <stdint.h>

#include "base64.h"

static char base64chars[65] = {
	"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"
};

struct b64_dec_up *b64_dec_init(struct b64_dec_up *b64p)
{
	b64p->dec_finish = 0;
	b64p->dec_bitcnt = 0;
	b64p->dec_bitvalues = 0;

	b64p->dec_last_in = 0;
	b64p->dec_last_out = 0;
	return b64p;
}

static int get_b64val(const char code, int *valp)
{
	if (code == '+') {
		*valp = 62;
		return 1;
	}

	if (code == '/') {
		*valp = 63;
		return 1;
	}

	if ('A' <= code && code <= 'Z') {
		*valp = code - 'A';
		return 1;
	}

	if ('a' <= code && code <= 'z') {
		*valp = code - 'a' + 26;
		return 1;
	}

	if ('0' <= code && code <= '9') {
		*valp = code - '0' + 52;
		return 1;
	}

	return 0;
}

int b64_dec_trans(struct b64_dec_up *b64p,
		void *dst, size_t l_dst, const void *src, size_t l_src)
{
	size_t orig_dst = l_dst;
	size_t orig_src = l_src;

	uint8_t *dst1 = (uint8_t *)dst;
	const uint8_t *src1 = (const uint8_t *)src;

	while (l_src > 0 && l_dst > 0) {
		int value = 0;
		while (!get_b64val(*src1++, &value))
			if (--l_src == 0)
				goto dec_flush;

		l_src --;
		b64p->dec_bitcnt += 6;
		b64p->dec_bitvalues <<= 6;
		b64p->dec_bitvalues |= value;

		while (b64p->dec_bitcnt >= 8 && l_dst > 0) {
			b64p->dec_bitcnt -= 8;
			*dst1++ = (b64p->dec_bitvalues >> b64p->dec_bitcnt);
			l_dst --;
		}
	}

dec_flush:
	while (b64p->dec_bitcnt >= 8 && l_dst > 0) {
		b64p->dec_bitcnt -= 8;
		*dst1++ = (b64p->dec_bitvalues >> b64p->dec_bitcnt);
		l_dst --;
	}

	b64p->dec_last_in = (orig_src - l_src);
	b64p->dec_last_out = (orig_dst - l_dst);
	return 0;
}

int b64_dec_finish(struct b64_dec_up *b64p, void *dst, size_t l_dst)
{
	b64p->dec_finish = 1;
	return b64_dec_trans(b64p, dst, l_dst, 0, 0);
}

struct b64_enc_up *b64_enc_init(struct b64_enc_up * b64p)
{
	b64p->enc_total = 0;
	b64p->enc_finish = 0;
	b64p->enc_bitcnt = 0;
	b64p->enc_bitvalues = 0;

	b64p->enc_last_in = 0;
	b64p->enc_last_out = 0;
	return b64p;
}

int b64_enc_trans(struct b64_enc_up *b64p,
		void *dst, size_t l_dst, const void *src, size_t l_src)
{
	size_t orig_dst = l_dst;
	size_t orig_src = l_src;

	int index;
	uint8_t * dst1 = (uint8_t *)dst;
	const uint8_t * src1 = (const uint8_t *)src;

	while (l_src > 0 && l_dst > 0) {
		l_src --;
		b64p->enc_bitcnt += 8;
		b64p->enc_bitvalues <<= 8;
		b64p->enc_bitvalues |= *src1++;

		while (b64p->enc_bitcnt >= 6 && l_dst > 0) {
			b64p->enc_total++;
			b64p->enc_bitcnt -= 6;
			index = (b64p->enc_bitvalues >> b64p->enc_bitcnt);
			*dst1++ = base64chars[index & 0x3F];
			l_dst --;
		}
	}

	while (b64p->enc_bitcnt > 6 && l_dst > 0) {
		b64p->enc_total++;
		b64p->enc_bitcnt -= 6;
		index = (b64p->enc_bitvalues >> b64p->enc_bitcnt);
		*dst1++ = base64chars[index & 0x3F];
		l_dst --;
	}

	if (b64p->enc_finish && l_dst > 0) {
		if (b64p->enc_bitcnt > 0) {
			index = (b64p->enc_bitvalues << (6 - b64p->enc_bitcnt));
			*dst1++ = base64chars[index & 0x3F];
			b64p->enc_bitcnt = 0;
			b64p->enc_total++;
			l_dst --;
		}

		while (l_dst > 0 &&
				(b64p->enc_total & 0x3)) {
			b64p->enc_total++;
			*dst1++ = '=';
			l_dst --;
		}
	}

	b64p->enc_last_in = (orig_src - l_src);
	b64p->enc_last_out = (orig_dst - l_dst);
	return 0;
}

int b64_enc_finish(struct b64_enc_up *b64p, void *dst, size_t l_dst)
{
	b64p->enc_finish = 1;
	return b64_enc_trans(b64p, dst, l_dst, 0, 0);
}
