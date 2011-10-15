#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <string.h>

static const int MAX_PENDING = 5;

int main(int argc, char **argv) {
	int h_sock;
	int h_config;
	char *configName;

	struct sockaddr_in serverAddr;
	short listenPort;
	long listenAddr;

	if (argc != 2) {
		fprintf(stderr, "Usage: %s config_file\n", argv[0]);
		exit(-1);
	}

	configName = argv[1];
	listenPort = 10000;
	listenAddr = INADDR_ANY;


	// Open and parse the config file
	if ((h_config = open(configName, O_RDONLY)) < 0) {
		perror("Could not open config file");
		exit(-1);
	}

	// Create the listening socket for the server
	if ((h_sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
		perror("Could not create socket");
		exit(-1);
	}
	memset(&serverAddr, 0, sizeof(serverAddr));
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_addr.s_addr = htonl(listenAddr);
	serverAddr.sin_port = htons(listenPort);

	// Bind to the listening address
	if (bind(h_sock, (struct sockaddr *) &serverAddr, sizeof(serverAddr)) < 0) {
		perror("Could not bind");
		exit(-1);
	}

	if (listen(h_sock, MAX_PENDING) < 0) {
		perror("Could not listen");
		exit(-1);
	}

	for (int i = 0; i < 5; i++) {
		struct sockaddr_in clientAddr;
		socklen_t clientAddrLen = sizeof(clientAddr);
		int h_client;

		if ((h_client = accept(h_sock, (struct sockaddr *) &clientAddr, &clientAddrLen)) < 0) {
			perror("Could not accept");
			exit(-1);
		}

		printf("Accepted client %d\n", i + 1);
		send(h_client, "Hello\n", 7, 0);
		close(h_client);
	}

	return 0;
}

