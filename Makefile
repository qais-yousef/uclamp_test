TESTS=$(basename $(wildcard *.c))

CFLAGS=-Werror -Wall -g
LIBS=-lpthread

%: %.c
	$(CC) $(CFLAGS) -o $@ $< $(LIBS)

all: $(TESTS)

clean:
	rm -f *.o $(TESTS)
