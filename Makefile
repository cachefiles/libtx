RMR ?=rm -f
CFLAGS += -Iinclude
CXXFLAGS += $(CFLAGS)

LDLIBS += -lstdc++

TARGETS = txcat
XCLEANS = txcat.o
COREOBJ = tx_loop.o tx_timer.o tx_socket.o tx_platform.o
OBJECTS = $(COREOBJ) tx_poll.o tx_select.o \
		  tx_epoll.o tx_kqueue.o tx_completion_port.o

all: $(TARGETS)

txcat: txcat.o $(OBJECTS)

.PHONY: clean

clean:
	$(RM) $(OBJECTS) $(TARGETS) $(XCLEANS)

