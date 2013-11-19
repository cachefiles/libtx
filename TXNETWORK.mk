CFLAGS += -I. -Iinclude

all: cc_cubic.o cc_htcp.o cc_newreno.o cc_vegas.o cc_hd.o cc_chd.o cc_cdg.o cc.o tcp_debug.o

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

tcp_debug.o: netinet/tcp_debug.c
	$(CC) -c $(CFLAGS) $^ 

# tcp_debug.c      tcp_lro.c      tcp_reass.c  tcp_syncache.c  tcp_usrreq.c
# tcp_hostcache.c  tcp_offload.c  tcp_sack.c   tcp_timer.c
# tcp_input.c      tcp_output.c   tcp_subr.c   tcp_timewait.c

tcp_reass.o: netinet/tcp_reass.c
	$(CC) -c $(CFLAGS) $^ 

tcp_usrreq.o: netinet/tcp_usrreq.c
	$(CC) -c $(CFLAGS) $^ 

tcp_sack.o: netinet/tcp_sack.c
	$(CC) -c $(CFLAGS) $^ 

tcp_timer.o: netinet/tcp_timer.c
	$(CC) -c $(CFLAGS) $^ 

tcp_input.o: netinet/tcp_input.c
	$(CC) -c $(CFLAGS) $^ 

tcp_output.o: netinet/tcp_output.c
	$(CC) -c $(CFLAGS) $^ 

tcp_subr.o: netinet/tcp_subr.c
	$(CC) -c $(CFLAGS) $^ 

tcp_timewait.o: netinet/tcp_timewait.c
	$(CC) -c $(CFLAGS) $^ 
