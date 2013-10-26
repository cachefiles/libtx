CFLAGS = -Iinclude
CXXFLAGS = $(CFLAGS)

LDLIBS = -lstdc++

TARGETS = txcat
XCLEANS = txcat.o
OBJECTS = tx_loop.o tx_epoll.o tx_kqueue.o tx_io_completion_port.o

all: $(TARGETS)

txcat: txcat.o $(OBJECTS)

.PHONY: clean

clean:
	$(RM) -f $(OBJECTS) $(TARGETS) $(XCLEANS)
