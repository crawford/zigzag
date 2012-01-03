full:
	mkdir -p build
	gcc -ggdb3 -std=c99 -Wall -Wextra src/server.c src/node.c src/channel.c src/connection.c -o build/zigzag -lxbapi -ltalloc -DDEBUG
