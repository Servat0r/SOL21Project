#Generals
SHELL	:= /bin/bash
CC		=  gcc
CFLAGS	+= -Wall -g -pedantic -pthread -std=c99
#Main directories
SRC		= src
BIN		= bin
TEST	= test
TEST1	= $(TEST)/test1files
TEST2	= $(TEST)/test2files
TEST3	= $(TEST)/test3files
UNITTEST = $(TEST)/unittest
OBJS	= bin/objects
LIB		= bin/lib
INCLUDE	= src/include
TMP		= bin/tmp
#Threads library (POSIX threads)
LTHREAD	= -lpthread

.PHONY : all clean cleanall test1 test2
.SUFFIXES : .c .h .o

#Header library (WITHOUT a .c file)
hlib := $(wildcard $(SRC)/*.h)

#Common headers with a corresponding .c file
common_headers := $(INCLUDE)/util.h $(INCLUDE)/dir_utils.h $(INCLUDE)/argparser.h $(INCLUDE)/linkedlist.h $(INCLUDE)/protocol.h
#Server-only headers with a corresponding .c file
server_headers := $(INCLUDE)/fs.h $(INCLUDE)/fdata.h $(INCLUDE)/parser.h $(INCLUDE)/tsqueue.h $(INCLUDE)/server_support.h $(INCLUDE)/icl_hash.h
#Client-only headers with a corresponding .c file
client_headers := $(INCLUDE)/client_server_API.h
#ALL headers
headers := $(common_headers) $(server_headers) $(client_headers)

#Common objects
common_objs := $(common_headers:$(INCLUDE)/%.h=$(OBJS)/%.o)
#Server-only objects
server_objs := $(server_headers:$(INCLUDE)/%.h=$(OBJS)/%.o)
#Client-only objects
client_objs := $(client_headers:$(INCLUDE)/%.h=$(OBJS)/%.o)
#ALL objects
objects := $(common_objs) $(server_objs) $(client_objs)

#Unittest source files
src_unittests := $(wildcard $(UNITTEST)/*.c)
#Unittest binaries
bin_unittests := $(src_unittests:$(UNITTEST)/%.c=$(UNITTEST)/%)
#Dynamic linking path
dlpath := -Wl,-rpath,$(LIB)
#Included directories for compilation
includes := -I $(SRC)/ -I $(INCLUDE)/


#Generals

all :
	-mkdir -p bin/lib bin/objects bin/test bin/tmp; #Create bin folders if absent
	make server;
	make client
	
test1 :
	make all;
	test/test1.sh
	
test2 :
	make all;
	test/test2.sh
	
test3:
	make all;
	test/test3.sh

server : $(SRC)/server.c libserver.so
	$(CC) $(includes) $(CFLAGS) $< -o $(BIN)/$@ $(LTHREAD) $(dlpath) -L $(LIB)/ -lserver
	
client : $(SRC)/client.c libapi.so
	$(CC) $(includes) $(CFLAGS) $< -o $(BIN)/$@ $(LTHREAD) $(dlpath) -L $(LIB)/ -lapi

#Server library
libserver.so : $(server_objs) $(common_objs)
	$(CC) -shared $^ -o $(LIB)/$@  $(LTHREAD)

#Client library
libapi.so : $(client_objs) $(common_objs)
	$(CC) -shared $^ -o $(LIB)/$@ $(LTHREAD)

#ALL objects
$(objects) : $(OBJS)/%.o : $(SRC)/%.c $(INCLUDE)/%.h $(hlib) 
	$(CC) $(includes) -fPIC $(CFLAGS) -c $< -o $@ $(LTHREAD)  


clean :
	-rm -r -d $(BIN) $(TEST1)/test1dest1 $(TEST1)/test1dest2 $(TEST2)/bigrecvs $(TEST3)/recv_*

cleanall :
	-rm -r -d $(BIN) $(TEST1)/test1dest1 $(TEST1)/test1dest2 $(TEST2)/bigrecvs $(TEST3)/recv_*

