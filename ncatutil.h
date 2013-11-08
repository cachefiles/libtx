#ifndef __NCATUTIL_H_
#define __NCATUTIL_H_

struct netcat_t;
netcat_t* get_cat_context(int argc, char **argv);
const char *get_cat_options(netcat_t *upp, const char *name);

int get_cat_socket(netcat_t *upp);
int get_netcat_socket(int argc, char *argv[]);
#endif

