APP=uclamp_test

SRC=$(wildcard *.c)

CFLAGS=-Werror -Wall -g
LIBS=-lpthread

$(APP): $(SRC)
	$(CC) $(CFLAGS) -o $(APP) $(SRC) $(LIBS)

all: $(APP)

clean:
	rm -f *.o $(APP)
