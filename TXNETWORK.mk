CFLAGS += -I. -Iinclude

all: cc_cubic.o cc_htcp.o cc_newreno.o cc_vegas.o cc_hd.o cc_chd.o cc_cdg.o cc.o

cc_htcp.o: netinet/cc/cc_htcp.c
	$(CC) -c $(CFLAGS) $^ 

cc_vegas.o: netinet/cc/cc_vegas.c
	$(CC) -c $(CFLAGS) $^ 

cc_cubic.o: netinet/cc/cc_cubic.c
	$(CC) -c $(CFLAGS) $^ 

cc_newreno.o: netinet/cc/cc_newreno.c
	$(CC) -c $(CFLAGS) $^ 

cc.o: netinet/cc/cc.c
	$(CC) -c $(CFLAGS) $^ 

cc_cdg.o: netinet/cc/cc_cdg.c
	$(CC) -c $(CFLAGS) $^ 

cc_chd.o: netinet/cc/cc_chd.c
	$(CC) -c $(CFLAGS) $^ 

cc_hd.o: netinet/cc/cc_hd.c
	$(CC) -c $(CFLAGS) $^ 

