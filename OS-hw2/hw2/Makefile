CC = gcc
CFLAGS = -Wall -g -pthread

all: hw2

# ----------------------------------------------------------------------
# OPTION 1: Pure C Version (main.c + crossroad.c) -- DEFAULT
# ----------------------------------------------------------------------
hw2: main.o hw2_output.o crossroad.o
	$(CC) $(CFLAGS) -o hw2 main.o hw2_output.o crossroad.o

main.o: main.c crossroad.h hw2_output.h
	$(CC) $(CFLAGS) -c main.c

crossroad.o: crossroad.c crossroad.h hw2_output.h
	$(CC) $(CFLAGS) -c crossroad.c

hw2_output.o: hw2_output.c hw2_output.h
	$(CC) $(CFLAGS) -c hw2_output.c

clean:
	rm -f *.o hw2