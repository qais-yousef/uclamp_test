APP=uclamp_test

SRC=*.c

CFLAGS=-Werror

all:
	$(CC) $(CFLAGS) -o $(APP) $(SRC)

clean:
	rm -f *.o
