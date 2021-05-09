CC		=  gcc
CFLAGS	+= -Wall -g -pedantic -std=c99
#Main directories
SRC		= src
BIN		= bin
TEST	= test
OBJS	= bin/objects
LIB		= bin/lib
INCLUDE	= src/include
TMP		= bin/tmp
#Threads library (POSIX threads)
LTHREAD	= -lpthread
#Files to remove with 'clean'/'cleanall'
TARGETS	= $(wildcard $(BIN)/*) $(wildcard $(LIB)/*) $(wildcard $(OBJS)/*) $(wildcard $(TMP)/*)

.PHONY : clean unlink debug
.SUFFIXES : .c .h .o

#Headers with a corresponding .c file
headers := $(wildcard $(INCLUDE)/*.h)
#Header library (WITHOUT a .c file)
hlib := $(wildcard $(SRC)/*.h)
#Files .o from headers
objects := $(headers:$(INCLUDE)/%.h=$(OBJS)/%.o)
#Test source files
ctests := $(wildcard $(TEST)/*.c)
#Test binaries
tests := $(ctests:$(TEST)/%.c=$(TEST)/%)
#Final executables (apart from tests)
execs := server client
#Dynamic linking path
dlpath := -Wl,-rpath,$(LIB)
#Included directories for compilation
includes := -I $(SRC)/ -I $(INCLUDE)/


#Generals

all : execs $(tests)

test : $(tests)

debug :
	@echo headers = $(headers)
	@echo hlib = $(hlib)
	@echo objects = $(objects)
	@echo tests = $(tests)
	@echo execs = $(execs)
	@echo dlpath = $(dlpath)
	@echo includes = $(includes)
	@echo targets = $(TARGETS)

$(tests) : $(TEST)/% : $(TEST)/%.c libshared.so
	$(CC) $(includes) $(CFLAGS) $< -o $(BIN)/$@ $(LTHREAD) $(dlpath) -L $(LIB)/ -lshared

$(execs) : % : $(SRC)/%.c libshared.so
	$(CC) $(includes) $(CFLAGS) $< -o $(BIN)/$@ $(LTHREAD) $(dlpath) -L $(LIB)/ -lshared

$(objects) : $(OBJS)/%.o : $(SRC)/%.c $(INCLUDE)/%.h $(hlib) 
	$(CC) $(includes) -fPIC $(CFLAGS) -c $< -o $@ $(LTHREAD)  

%.so : $(objects)
	$(CC) -shared $^ -o $(LIB)/$@

clean :
	-rm $(TARGETS)

