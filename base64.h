#ifndef _BASE64_H_
#define _BASE64_H_

struct b64_dec_up {
	int dec_finish;
	int dec_bitcnt;
	int dec_bitvalues;

	int dec_last_in;
	int dec_last_out;
};

struct b64_dec_up *b64_dec_init(struct b64_dec_up *b64p);
int b64_dec_trans(struct b64_dec_up *b64p,
		void *dst, size_t ldst, const void *src, size_t lsrc);
int b64_dec_finish(struct b64_dec_up *b64p, void *dst, size_t ldst);

struct b64_enc_up {
	int enc_total;
	int enc_finish;
	int enc_bitcnt;
	int enc_bitvalues;

	int enc_last_in;
	int enc_last_out;
};

struct b64_enc_up *b64_enc_init(struct b64_enc_up * b64p);
int b64_enc_trans(struct b64_enc_up *b64p,
		void *dst, size_t ldst, const void *src, size_t lsrc);
int b64_enc_finish(struct b64_enc_up *b64p, void *dst, size_t ldst);

#endif
