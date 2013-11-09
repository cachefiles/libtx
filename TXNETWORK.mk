CFLAGS += -I. -Iinclude

all:
	$(CC) -c $(CFLAGS) netinet/cc/cc_cubic.c
	$(CC) -c $(CFLAGS) netinet/cc/cc_newreno.c
