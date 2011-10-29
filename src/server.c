#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <string.h>
#include <signal.h>
#include <poll.h>
#include "client_node.h"

static const int MAX_PENDING = 5;

void handle_sigterm();
int setup_listening_socket(uint32_t, uint16_t);
int accept_connection(int, client_root *);
int append_to_fds(struct pollfd **, nfds_t *, int);
int rebuild_fds(struct pollfd **, nfds_t *, client_root *);

char running = 1;

int main(int argc, char **argv) {
	int h_sock;
	int h_config;
	char *configName = NULL;

	uint16_t listenPort;
	uint32_t listenAddr;

	struct pollfd *fds = NULL;
	nfds_t nfds = 0;

	if (signal(SIGINT, handle_sigterm) == SIG_ERR) {
		perror("signal()");
		exit(-1);
	}
	if (signal(SIGTERM, handle_sigterm) == SIG_ERR) {
		perror("signal()");
		exit(-1);
	}

	if (argc != 2) {
		fprintf(stderr, "Usage: %s config_file\n", argv[0]);
		exit(-1);
	}

	client_root *clients = malloc(sizeof(client_root));
	if (clients == NULL) {
		perror("malloc()");
		exit(-1);
	}
	memset(clients, 0, sizeof(client_root));

	// Open and parse the config file
	configName = argv[1];
	listenPort = 10000;
	listenAddr = INADDR_ANY;

	if ((h_config = open(configName, O_RDONLY)) < 0) {
		perror("open()");
		exit(-1);
	}

	h_sock = setup_listening_socket(listenAddr, listenPort);

	fds = malloc(sizeof(struct pollfd));
	if (fds == NULL) {
		perror("malloc()");
		exit(-1);
	}
	nfds = 1;

	fds[0].fd = h_sock;
	fds[0].events = POLLIN;

	while (running) {
		int result = poll(fds, nfds, 1000);
		switch (result) {
			case -1:
				perror("poll()");
				continue;
			case 0:
				continue;
		}

		printf("Something happened\n");

		struct pollfd *fd = fds;
		// Check the first file descriptor (the listening socket) for readable
		if (fd->revents) {
			if (fd->revents & POLLIN) {
				int h_client = accept_connection(fd->fd, clients);
				if (append_to_fds(&fds, &nfds, h_client) < 0) {
					// We couldn't add the client to the list of file descriptors
					// so send an error and disconnect
					send(h_client, "Fuck\n", 5, 0);
					close(h_client);
					destroy_node_by_node(clients, clients->tail);
				}
			}
			result--;
		}

		// Make sure we have any client sockets to check
		//if (nfds <= 1) {
		//	continue;
		//}

		// Check the remaining file descriptors (the client sockets) for events
		char dirty = 0;
		while (++fd, result > 0) {
			if (fd->revents) {
				result--;

				if (fd->revents & POLLERR) {
					fprintf(stderr, "Error occured on the socket\n");
				}
				if (fd->revents & POLLHUP) {
					printf("Disconnected\n");

					destroy_node_by_fd(clients, fd->fd);
					dirty = 1;

					close(fd->fd);
					continue;
				}
				if (fd->revents & POLLIN) {
					char buf[100];
					int result = read(fd->fd, &buf, 101);

					if (result == -1) {
						perror("read()");
						continue;
					}

					buf[result] = 0;
					printf("%s\n", buf);
				}
			}
		}

		if (dirty) {
			rebuild_fds(&fds, &nfds, clients);
		}
	}

	close(h_sock);

	return 0;
}

int accept_connection(int h_sock, client_root *clients) {
	struct sockaddr_in clientAddr;
	socklen_t clientAddrLen = sizeof(clientAddr);
	int h_client;

	if ((h_client = accept(h_sock, (struct sockaddr *) &clientAddr, &clientAddrLen)) < 0) {
		perror("accept()");
		exit(-1);
	}

	client_node *node = create_node(clients);
	if (node == NULL) {
		perror("malloc()");
		return -1;
	}

	node->h_socket = h_client;
	printf("Accepted client %d\n", clients->count);

	send(h_client, "Hello\n", 7, 0);

	return h_client;
}

int append_to_fds(struct pollfd **fds, nfds_t *size, int fd) {
	struct pollfd *new = malloc(sizeof(struct pollfd) * (*size + 1));
	if (new == NULL) {
		perror("malloc()");
		return -1;
	}

	memcpy(new, *fds, (*size) * sizeof(struct pollfd));
	free(*fds);
	*fds = new;

	(*fds)[*size].fd = fd;
	(*fds)[*size].events = POLLIN;

	(*size)++;

	return 0;
}

int rebuild_fds(struct pollfd **fds, nfds_t *size, client_root *clients) {
	struct pollfd *new = malloc((clients->count + 1) * sizeof(struct pollfd));
	if (new == NULL) {
		perror("malloc()");
		return -1;
	}

	// Copy the listening socket to the new list of file descriptors
	new[0].fd = (*fds)[0].fd;
	new[0].events = (*fds)[0].events;

	// Swap out the old for the new list
	free(*fds);
	*fds = new;
	*size = clients->count + 1;

	// Iterate through all of the connected nodes and create file descriptor
	// entries for them.
	client_node *node = clients->head;
	struct pollfd *fd = new + 1;
	while (node) {
		fd->fd = node->h_socket;
		fd->events = POLLIN;

		fd++;
		node = node->next;
	}

	return 0;
}

int setup_listening_socket(uint32_t listenAddr, uint16_t listenPort) {
	int h_sock;
	struct sockaddr_in serverAddr;

	// Create the listening socket for the server
	if ((h_sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
		perror("socket()");
		exit(-1);
	}
	memset(&serverAddr, 0, sizeof(serverAddr));
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_addr.s_addr = htonl(listenAddr);
	serverAddr.sin_port = htons(listenPort);

	// Bind to the listening address
	if (bind(h_sock, (struct sockaddr *) &serverAddr, sizeof(serverAddr)) < 0) {
		perror("bind()");
		exit(-1);
	}

	// Set the listending socket to be non-blocking
	if (fcntl(h_sock, F_SETFL, O_NONBLOCK)) {
		perror("fcntl()");
		exit(-1);
	}

	if (listen(h_sock, MAX_PENDING) < 0) {
		perror("listen()");
		exit(-1);
	}

	return h_sock;
}

void handle_sigterm(int sig) {
	(void)sig;
	running = 0;
	printf("Terminating daemon\n");
}

