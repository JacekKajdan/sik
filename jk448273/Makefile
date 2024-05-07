CC     = gcc
CFLAGS = -Wall -Wextra -O2 -std=gnu17
LFLAGS =

.PHONY: all clean

TARGET1 = ppcbc
TARGET2 = ppcbs

all: $(TARGET2) $(TARGET1) 

$(TARGET1): $(TARGET1).o prot.o err.o common.o
$(TARGET2): $(TARGET2).o prot.o err.o common.o


err.o: err.c err.h
common.o: common.c err.h common.h

prot.o: prot.c prot.h
ppcbc.o: ppcbc.c prot.h err.h common.h protconst.h
ppcbs.o: ppcbs.c prot.h err.h common.h protconst.h

