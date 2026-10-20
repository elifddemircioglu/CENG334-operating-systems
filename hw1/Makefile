CXX      = g++
CXXFLAGS = -Wall -std=c++17
CC       = gcc
CFLAGS   = -Wall

all: merger

merger: merger.cpp merger_parser.o
	$(CXX) $(CXXFLAGS) -o merger merger.cpp merger_parser.o

merger_parser.o: merger_parser.c merger_parser.h common.h
	$(CC) $(CFLAGS) -c merger_parser.c

clean:
	rm -f merger merger_parser.o
