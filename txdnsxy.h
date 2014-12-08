#ifndef _TXDNSXY_H_
#define _TXDNSXY_H_
int add_fakedn(const char *dn);
int add_fakeip(unsigned int ip);
int add_fakenet(unsigned int network, unsigned int netmask);

int add_localdn(const char *dn);
int add_localnet(unsigned int network, unsigned int netmask);

int set_translate(int mode);
#define TRANSLATE_WHITELIST 0x01
#define TRANSLATE_BLACKLIST 0x02
int set_fuckingip(unsigned int ip);
#endif
