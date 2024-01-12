CC = clang
CFLAGS = -Wall -Wpedantic -Werror -Wextra
LDFLAGS = -pthread

all: httpserver

httpserver: httpserver.o
	$(CC) -o httpserver httpserver.o $(LDFLAGS)

httpserver.o: httpserver.c
	$(CC) $(CFLAGS) -c httpserver.c

clean:
	rm -f httpserver httpserver.o

format:
	clang-format -i -style=file *.[ch]