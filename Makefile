CPP = cc

#-I../uw-imap/imap-2007f/c-client
CFLAGS = -Wall -g -O0 -fno-operator-names -std=c++11

#/home/tim/uw-imap/imap-2007f/c-client/c-client.a
LIBS = -L/usr/local/lib -lstdc++ -lfuse -lssl -ldl -lm -lpam -lvmime

OBJS = imap.o imapfs.o stack_trace.o crash_handler.o fs_log.o

default: imap

%.o: %.cpp
	$(CPP) $(CFLAGS) -c $< -o $@

.PHONY: clean

clean:
	rm -f *.o *~ core imap


imap: $(OBJS)
	$(CPP) $(OBJS) $(LIBS) -o imap

