CPP = cc

#-I../uw-imap/imap-2007f/c-client
CFLAGS = -Wall -rdynamic -ggdb3 -O0 -fno-operator-names -std=c++11 -I/usr/local/include

#/home/tim/uw-imap/imap-2007f/c-client/c-client.a
LIBS = /usr/local/lib/libvmime.so -lstdc++ -lfuse -lssl -ldl -lm -lpam

OBJS = stack_trace.o crash_handler.o fs_log.o

default: imap

%.o: %.cpp
	$(CPP) $(CFLAGS) -c $< -o $@

.PHONY: clean

clean:
	rm -f *.o *~ core imap


imap: $(OBJS) imap.o imapfs.o
	$(CPP) -rdynamic $(OBJS) imap.o imapfs.o $(LIBS) -o imap

#passthrough: $(OBJS) passthrough.o
#	$(CPP) -rdynamic $(OBJS) passthrough.o $(LIBS) -o passthrough
