#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <string.h>
#include <signal.h>
#include <poll.h>
#include <errno.h>
#include <xbapi.h>
#include <termios.h>
#include <talloc.h>
#include "node.h"
#include "connection.h"
#include "channel.h"

//#define DEBUG
//#define NO_HARDWARE

static const int MAX_PENDING = 5;
static const int READ_BUF_LEN = 100;
static const char *MESSAGE_DELIMITER_S = " ";
static const char MESSAGE_DELIMITER_C = ' ';
static const char MESSAGE_START = '$';
static const char *MESSAGE_SUB = "sub";
static const char *MESSAGE_SEND = "send";
static const char *MESSAGE_RES = "res";
static const char *MESSAGE_TRUE = "true";
static const char *MESSAGE_FALSE = "false";

void handle_sigterm();
int setup_listening_socket(uint32_t, uint16_t);
int setup_serial_connection(char *device);
int accept_connection(int, root_node_t *);
bool append_to_fds(struct pollfd **, nfds_t *, int);
bool rebuild_fds(struct pollfd **, nfds_t *, root_node_t *);
bool process_client_message(xbapi_conn_t *, xbapi_op_set_t *, root_node_t *, node_t *, char *, ssize_t);
node_t *find_client_by_fd(root_node_t *, int);
node_t *find_channel_by_id(root_node_t *, uint64_t);
bool convert_zid(char *, uint64_t *);
bool convert_msglen(char *, unsigned int *);
node_t *find_node_by_fd(root_node_t *, int);
void disconnect(root_node_t *, root_node_t *, int);
void exit_with_cleanup(int, int, ...);
bool send_response(int, char *, char *);

void handle_node_connected(xbapi_node_identification_t *node, void *user_data);
void handle_transmit_completed(xbapi_tx_status_t *status, void *user_data);
void handle_received_packet(xbapi_rx_packet_t *packet, void *user_data);
void handle_modem_changed(xbapi_modem_status_e status, void *user_data);
bool handle_operation_completed(xbapi_op_t *op, void *user_data);

typedef struct {
	connection_t *conn;
	char *msgid;
} op_data_t;


char running = 1;

int main(int argc, char **argv) {
	int h_sock = -1;
	int h_zigbee = -1;
	int h_config = -1;
	char *configName = NULL;

	uint16_t listenPort;
	uint32_t listenAddr;

	struct pollfd *fds = NULL;
	nfds_t nfds = 0;

	root_node_t *clients;
	root_node_t *channels;

	xbapi_conn_t *conn = NULL;
	xbapi_op_set_t *opset = xbapi_init_op_set();
#ifndef NO_HARDWARE
	xbapi_callbacks_t callbacks = {
		.node_connected = &handle_node_connected,
		.transmit_completed = &handle_transmit_completed,
		.received_packet = &handle_received_packet,
		.modem_changed = &handle_modem_changed,
		.operation_completed = &handle_operation_completed
	};
#endif

	if (signal(SIGINT, handle_sigterm) == SIG_ERR) {
		perror("signal()");
		exit_with_cleanup(-1, 0);
	}
	if (signal(SIGTERM, handle_sigterm) == SIG_ERR) {
		perror("signal()");
		exit_with_cleanup(-1, 0);
	}

	if (argc != 2) {
		fprintf(stderr, "Usage: %s serial_pipe\n", argv[0]);
		exit_with_cleanup(-1, 0);
	}

	// Create the list of client connections
	clients = malloc(sizeof(root_node_t));
	if (clients == NULL) {
		perror("malloc()");
		exit_with_cleanup(-1, 0);
	}
	memset(clients, 0, sizeof(root_node_t));

	// Create the list of channels
	channels = malloc(sizeof(root_node_t));
	if (channels == NULL) {
		perror("malloc()");
		exit_with_cleanup(-1, 0);
	}
	memset(channels, 0, sizeof(root_node_t));

	// Open and parse the config file
	configName = argv[1];
	listenPort = 10000;
	listenAddr = INADDR_ANY;

	if ((h_config = open(configName, O_RDONLY)) < 0) {
		perror("open()");
		exit_with_cleanup(-1, 0);
	}

	h_sock = setup_listening_socket(listenAddr, listenPort);

#ifdef NO_HARDWARE
	h_zigbee = 0;

	fds = malloc(sizeof(struct pollfd));
	if (fds == NULL) {
		perror("malloc()");
		exit_with_cleanup(-1, h_config, h_sock, h_zigbee);
	}
	nfds = 1;
#else
	h_zigbee = setup_serial_connection(argv[1]);
	if (h_sock < 0 || h_zigbee < 0) exit_with_cleanup(-1, h_config, h_sock, h_zigbee);
	conn = xbapi_init_conn(h_zigbee);

	fds = malloc(2 * sizeof(struct pollfd));
	if (fds == NULL) {
		perror("malloc()");
		exit_with_cleanup(-1, h_config, h_sock, h_zigbee);
	}
	nfds = 2;

	fds[1].fd = h_zigbee;
	fds[1].events = POLLIN;
#endif

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

		struct pollfd *fd = fds;
		// Check the first file descriptor (the listening socket) for readable
		if (fd->revents) {
			if (fd->revents & POLLIN) {
				int h_client = accept_connection(fd->fd, clients);
				if (!append_to_fds(&fds, &nfds, h_client)) {
					// We couldn't add the client to the list of file descriptors
					// so send an error and disconnect
					send(h_client, "Fuck\n", 5, 0);
					close(h_client);
					destroy_node(clients, clients->tail);
				}
			}
			result--;
		}

#ifndef NO_HARDWARE
		fd++;
		// Check the second file descriptor (the zigbee pipe) for readable
		if (fd->revents) {
			printf("xb (%d)\n", fd->revents);
			if (fd->revents & POLLIN) {
				printf("xtb\n");
				xbapi_rc_t rc = xbapi_process_data(conn, opset, &callbacks, channels);
				if (xbapi_errno(rc) != XBAPI_ERR_NOERR) {
					fprintf(stderr, "xbapi_process_data(): %s\n", xbapi_strerror(rc));
				}
			}
			result--;
		}
#endif

		// Check the remaining file descriptors (the client sockets) for events
		char dirty = 0;
		while (++fd, result > 0) {
			if (fd->revents) {
				result--;

				if (fd->revents & POLLERR) {
					fprintf(stderr, "Error occured on the socket\n");
				}
				if (fd->revents & POLLHUP) {
					disconnect(clients, channels, fd->fd);
					dirty = 1;
					continue;
				}
				if (fd->revents & POLLIN) {
					char buf[READ_BUF_LEN];
					ssize_t result = read(fd->fd, &buf, READ_BUF_LEN - 1);

					if (result == -1) {
						perror("read()");
						continue;
					}

					if (result == 0) {
						disconnect(clients, channels, fd->fd);
						dirty = 1;
						continue;
					}

					buf[result] = '\0';

					if (!process_client_message(conn, opset, channels, find_client_by_fd(clients, fd->fd), buf, result)) {
						printf("Received invalid message\n");
					}
				}
			}
		}

		// Rebuild the file descriptor list if the list of client sockets has changed
		if (dirty) {
			rebuild_fds(&fds, &nfds, clients);
		}
	}

	close(h_sock);

	return 0;
}

int accept_connection(int h_sock, root_node_t *clients) {
	struct sockaddr_in clientAddr;
	socklen_t clientAddrLen = sizeof(clientAddr);
	int h_client;

	if ((h_client = accept(h_sock, (struct sockaddr *) &clientAddr, &clientAddrLen)) < 0) {
		perror("accept()");
		return -1;
	}

	node_t *node = create_node(clients);
	if (node == NULL) {
		perror("malloc()");
		return -1;
	}

	node->data = malloc(sizeof(connection_t));
	if (node->data == NULL) {
		perror("malloc()");
		destroy_node(clients, node);
		return -1;
	}
	connection_t *conn = node->data;
	conn->h_socket = h_client;
	conn->pending_operations = malloc(sizeof(root_node_t));
	if (conn->pending_operations == NULL) {
		perror("malloc()");
		free(node->data);
		destroy_node(clients, node);
		return -1;
	}
	memset(conn->pending_operations, 0, sizeof(root_node_t));

	printf("Accepted client %d\n", clients->count);

	send(h_client, "Hello\n", 7, 0);

	return h_client;
}

bool append_to_fds(struct pollfd **fds, nfds_t *size, int fd) {
	struct pollfd *new = realloc(*fds, sizeof(struct pollfd) * (*size + 1));
	if (new == NULL) {
		perror("malloc()");
		return false;
	}

	*fds = new;

	(*fds)[*size].fd = fd;
	(*fds)[*size].events = POLLIN;

	(*size)++;

	return true;
}

bool rebuild_fds(struct pollfd **fds, nfds_t *size, root_node_t *clients) {
#ifdef NO_HARDWARE
	struct pollfd *new = realloc(*fds, (clients->count + 1) * sizeof(struct pollfd));
#else
	struct pollfd *new = realloc(*fds, (clients->count + 2) * sizeof(struct pollfd));
#endif
	if (new == NULL) {
		perror("malloc()");
		return false;
	}

	*fds = new;
#ifdef NO_HARDWARE
	*size = clients->count + 1;
#else
	*size = clients->count + 2;
#endif

	// Iterate through all of the connected nodes and create file descriptor
	// entries for them.
	node_t *node = clients->head;
#ifdef NO_HARDWARE
	struct pollfd *fd = *fds + 1;
#else
	struct pollfd *fd = *fds + 2;
#endif
	while (node != NULL) {
		fd->fd = ((connection_t *)node->data)->h_socket;
		fd->events = POLLIN;

		fd++;
		node = node->next;
	}

	return true;
}

int setup_listening_socket(uint32_t listenAddr, uint16_t listenPort) {
	int h_sock;
	struct sockaddr_in serverAddr;

	// Create the listening socket for the server
	if ((h_sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
		perror("socket()");
		return -1;
	}
	memset(&serverAddr, 0, sizeof(serverAddr));
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_addr.s_addr = htonl(listenAddr);
	serverAddr.sin_port = htons(listenPort);

	// Bind to the listening address
	if (bind(h_sock, (struct sockaddr *) &serverAddr, sizeof(serverAddr)) < 0) {
		perror("bind()");
		return -1;
	}

	// Set the listending socket to be non-blocking
	if (fcntl(h_sock, F_SETFL, O_NONBLOCK)) {
		perror("fcntl()");
		return -1;
	}

	if (listen(h_sock, MAX_PENDING) < 0) {
		perror("listen()");
		return -1;
	}

	return h_sock;
}

int setup_serial_connection(char *device) {
	int h_serial = open(device, O_RDWR | O_NOCTTY | O_NDELAY);
	if (h_serial == -1) {
		perror("open()");
		return -1;
	}
	//if (fcntl(h_serial, F_SETFL, FNDELAY) == -1) {
	//	perror("fcntl()");
	//}

	struct termios tio;
	tio.c_cflag = B9600 | CS8 | CSTOPB | CLOCAL | CREAD;
	tio.c_iflag = IGNPAR;
	tio.c_oflag = 0;
	tio.c_lflag = 0;
	tio.c_cc[VMIN] = 1;
	tio.c_cc[VTIME] = 0;
	if (tcflush(h_serial, TCIFLUSH) < 0) {
		perror("tcflush()");
		return -1;
	}
	if (tcsetattr(h_serial, TCSANOW, &tio) < 0) {
		perror("tcsetattr()");
		//return -1;
	}

	return h_serial;
}

void handle_sigterm(int sig) {
	(void)sig;
	running = 0;
	printf("Terminating daemon\n");
}

bool process_client_message(xbapi_conn_t *conn, xbapi_op_set_t *opset, root_node_t *channels, node_t *client, char *message, ssize_t msg_len) {
	if (message[0] != MESSAGE_START) return false;
	message++;
	msg_len--;

	char *f_cmd = strtok(message, MESSAGE_DELIMITER_S);
	char *f_zid = strtok(NULL, MESSAGE_DELIMITER_S);

	if (f_cmd == NULL || f_zid == NULL) {
		printf("Malformed command\n");
		send_response(((connection_t *)client->data)->h_socket, NULL, "malformed command");
		return false;
	}

	uint64_t v_zid;
	if (!convert_zid(f_zid, &v_zid)) {
		printf("Invalid ZID (%s)\n", f_zid);
		send_response(((connection_t *)client->data)->h_socket, NULL, "invalid ZID");
		return false;
	}

	if (strcmp(f_cmd, MESSAGE_SEND) == 0) {
		char *f_msgid = strtok(NULL, MESSAGE_DELIMITER_S);
		char *f_msglen = strtok(NULL, MESSAGE_DELIMITER_S);
		char *f_msg = strtok(NULL, MESSAGE_DELIMITER_S);

		if (f_msgid == NULL || f_msglen == NULL || f_msg == NULL) {
			printf("Malformed 'send' command\n");
			send_response(((connection_t *)client->data)->h_socket, f_msgid, "malformed 'send' command");
			return false;
		}

		unsigned int v_msglen;
		if (!convert_msglen(f_msglen, &v_msglen)) return false;

		if (strlen(f_msg) < v_msglen || v_msglen == 0) {
			printf("Message payload is too short\n");
			send_response(((connection_t *)client->data)->h_socket, f_msgid, "message payload is too short");
			return false;
		} else if (strlen(f_msg) > v_msglen) {
			f_msg[v_msglen] = '\0';
		}

		// Copy the message into a talloc'd buffer
		uint8_t *b_msg = talloc_array(NULL, uint8_t, v_msglen);
		if (b_msg == NULL) {
			send_response(((connection_t *)client->data)->h_socket, f_msgid, "internal error");
			return false;
		}
		memcpy(b_msg, f_msg, v_msglen);

		printf("Sending message (%s) to zigbee (0x%llX)\n", f_msg, v_zid);
#ifdef NO_HARDWARE
		(void)conn;
		(void)opset;
		send_response(((connection_t *)client->data)->h_socket, f_msgid, NULL);
#else
		xbapi_op_t *op;
		xbapi_rc_t rc = xbapi_transmit_data(conn, opset, b_msg, v_zid, &op);

		size_t f_msgid_len = strlen(f_msgid);
		char *b_msgid = malloc(sizeof(char) * f_msgid_len);
		if (b_msgid == NULL) return false;
		memcpy(b_msgid, f_msgid, f_msgid_len);

		op_data_t *op_data = malloc(sizeof(op_data_t));
		if (op_data == NULL) return false;
		op_data->conn = client->data;
		op_data->msgid = b_msgid;

		set_user_data(op, op_data);
		if (!add_operation_to_connection((connection_t *)client->data, op)) {
			perror("add_operation_to_connection()");
			return false;
		}

		if (xbapi_errno(rc) != XBAPI_ERR_NOERR) {
			static const size_t ERROUT_LEN = 50;
			char errout[ERROUT_LEN];
			if (snprintf(errout, ERROUT_LEN, "xbapi error (%s)", xbapi_strerror(rc)) != -1) {
				send_response(((connection_t *)client->data)->h_socket, f_msgid, errout);
				free(errout);
			}
		}
#endif
		return true;
	} else if (strcmp(f_cmd, MESSAGE_SUB) == 0) {
		node_t *channel_node = find_channel_by_id(channels, v_zid);
		if (channel_node == NULL) {
			// Couldn't find the specified channel, so create it
			channel_t *channel = create_channel(v_zid);
			if (channel == NULL) {
				printf("Couldn't create channel (%lld)\n", v_zid);
				send_response(((connection_t *)client->data)->h_socket, NULL, "internal error");
				return false;
			}
			channel_node = create_node(channels);
			if (channel_node == NULL) {
				printf("Couldn't create channel (%lld)\n", v_zid);
				send_response(((connection_t *)client->data)->h_socket, NULL, "internal error");
				return false;
			}
			channel_node->data = channel;
			printf("Created channel (%lld)\n", v_zid);
		} else {
			// Check to make sure that the client hasn't
			// already subscribed to this channel
			root_node_t *root = ((channel_t *)channel_node->data)->subscribers;
			destroy_node(root, find_node_by_fd(root, ((connection_t *)client->data)->h_socket));
		}

		node_t *subscriber_node = create_node(((channel_t *)channel_node->data)->subscribers);
		if (subscriber_node == NULL) {
			printf("Couldn't add subscriber to channel\n");
			send_response(((connection_t *)client->data)->h_socket, NULL, "internal error");
			return false;
		}
		printf("Subscribed %p to channel %lld\n", client, v_zid);
		subscriber_node->data = client->data;

		printf("%d clients subscribed to channel %lld\n", ((channel_t *)channel_node->data)->subscribers->count, v_zid);
		send_response(((connection_t *)client->data)->h_socket, NULL, NULL);
		return true;
	} else {
		printf("Unrecognized command '%s'\n", f_cmd);
		send_response(((connection_t *)client->data)->h_socket, NULL, "unrecognized command");
		return false;
	}
}

node_t *find_client_by_fd(root_node_t *root, int fd) {
	node_t *node = root->head;

	while (node != NULL) {
		if (((connection_t *)node->data)->h_socket == fd) {
			return node;
		}
		node = node->next;
	}

	return NULL;
}

node_t *find_channel_by_id(root_node_t *root, uint64_t id) {
	node_t *node = root->head;

	while (node != NULL) {
		if (((channel_t *)node->data)->id == id) {
			return node;
		}
		node = node->next;
	}

	return NULL;
}

// Note: This assumes that the 'long' type is at least 8 bytes
bool convert_zid(char *strzid, uint64_t *zid) {
	char *end;
	long long tzid;

	errno = 0;
	tzid = strtoll(strzid, &end, 16);

	if (errno || *end != '\0' || strlen(strzid) != 16) {
		printf("Invalid Zigbee Address\n");
		if (errno) {
			perror("strtoll()");
		}
		return false;
	}

	*zid = (uint64_t)tzid;
	return true;
}

bool convert_msglen(char *strmsglen, unsigned int *msglen) {
	char *end;
	long tmsglen;

	errno = 0;
	tmsglen = strtol(strmsglen, &end, 10);

	if (errno || *end != '\0') {
		printf("Invalid Message Length\n");
		if (errno) {
			perror("strtol()");
		}
		return false;
	}

	*msglen = (unsigned int)tmsglen;
	return true;
}

node_t *find_node_by_fd(root_node_t *root, int fd) {
	node_t *node = root->head;
	while (node != NULL) {
		if (((connection_t *)node->data)->h_socket == fd) {
			return node;
		}
		node = node->next;
	}
	return NULL;
}

void disconnect(root_node_t *clients, root_node_t *channels, int fd) {
	printf("Disconnected\n");

	node_t *client = find_node_by_fd(clients, fd);

	// Set each of the operation's user_data to NULL to prevent the callback from segfaulting
	node_t *op = ((connection_t *)client->data)->pending_operations->head;
	while (op != NULL) {
		set_user_data((xbapi_op_t *)op->data, NULL);
		op = op->next;
	}

	destroy_connection(client->data);
	destroy_node(clients, client);

	// Remove the client from all of the channel subscriptions
	node_t *next = channels->head;
	while (next != NULL) {
		node_t *cur = next;
		next = cur->next;

		root_node_t *root = ((channel_t *)cur->data)->subscribers;
		destroy_node(root, find_node_by_fd(root, fd));

		// Remove the channel if it is empty
		if (((channel_t *)cur->data)->subscribers->count == 0) {
			printf("Removing empty channel (%lld)\n", ((channel_t *)cur->data)->id);
			destroy_node(channels, cur);
		}
	}


	close(fd);
}

void exit_with_cleanup(int code, int fd, ...) {
	int *fds = &fd;
	while (*fds) {
		if (*fds > 0) close(*fds);
		fds++;
	}

	exit(code);
}

bool send_response(int fd, char *msgid, char *errorstr) {
	const char *resstr = (errorstr == NULL) ? MESSAGE_TRUE : MESSAGE_FALSE;

	size_t res_len = strlen(MESSAGE_RES);
	size_t msgid_len = (msgid == NULL) ? 0 : strlen(msgid);
	size_t resstr_len = strlen(resstr);
	size_t errorstr_len = (errorstr == NULL) ? 0 : strlen(errorstr);

	size_t response_len = 5 + res_len + msgid_len + resstr_len + errorstr_len;
	char *response = malloc(sizeof(char) * response_len);
	if (response == NULL) return false;

	response[0] = MESSAGE_START;
	response[1] = MESSAGE_DELIMITER_C;
	memcpy(response + 2, MESSAGE_RES, res_len);
	response[2 + res_len] = MESSAGE_DELIMITER_C;
	memcpy(response + 3 + res_len, msgid, msgid_len);
	response[3 + res_len + msgid_len] = MESSAGE_DELIMITER_C;
	memcpy(response + 4 + res_len + msgid_len, resstr, resstr_len);
	response[4 + res_len + msgid_len + resstr_len] = MESSAGE_DELIMITER_C;
	memcpy(response + 5 + res_len + msgid_len + resstr_len, errorstr, errorstr_len);

	ssize_t res;
	for (size_t pos = 0; pos < response_len; pos += res) {
		res = send(fd, response + pos, response_len - pos, 0);
		if (res < 0) return false;
	}

	return true;
}


void handle_node_connected(xbapi_node_identification_t *node, void *user_data) {
	(void)user_data;
#ifdef DEBUG
	printf("Source Address: %llX\n", node->source_address);
	printf("Source Network Address: %X\n", node->source_network_address);
	switch (node->receive_options) {
		case XBAPI_RX_OPT_ACKNOWLEDGE:
			printf("Receive Options: Acknowledge\n");
			break;
		case XBAPI_RX_OPT_BROADCAST:
			printf("Receive Options: Broadcast\n");
			break;
		case XBAPI_RX_OPT_INVALID:
			printf("Receive Options: Invalid\n");
	}
	printf("Remote Address: %llX\n", node->remote_address);
	printf("Remote Network Address: %X\n", node->remote_network_address);
	printf("Node Identifier: %s\n", node->node_identifier);
	printf("Parent Network Address: %X\n", node->parent_network_address);
	switch (node->device_type) {
		case XBAPI_DEVICE_TYPE_COORDINATOR:
			printf("Device Type: Coordinator\n");
			break;
		case XBAPI_DEVICE_TYPE_ROUTER:
			printf("Device Type: Router\n");
			break;
		case XBAPI_DEVICE_TYPE_END_DEVICE:
			printf("Device Type: End Device\n");
			break;
		case XBAPI_DEVICE_TYPE_INVALID:
			printf("Device Type: Invalid\n");
	}
	switch (node->source_event) {
		case XBAPI_SOURCE_EVENT_PUSHBUTTON:
			printf("Source Event: Pushbutton\n");
			break;
		case XBAPI_SOURCE_EVENT_JOINED:
			printf("Source Event: Joined\n");
			break;
		case XBAPI_SOURCE_EVENT_POWER_CYCLE:
			printf("Source Event: Power Cycle\n");
			break;
		case XBAPI_SOURCE_EVENT_INVALID:
			printf("Source Event: Invalid\n");
	}
	printf("Profile ID: %X\n", node->profile_id);
	printf("Manufacturer ID: %X\n", node->manufacturer_id);
	printf("\n");
#else
	(void)node;
#endif
}

void handle_transmit_completed(xbapi_tx_status_t *status, void *user_data) {
	(void)user_data;
#ifdef DEBUG
	printf("Delivery Network Address: %X\n", status->delivery_network_address);
	printf("Retry Count: %d\n", status->retry_count);
	switch (status->delivery_status) {
		case XBAPI_DELIVERY_STATUS_SUCCESS:
			printf("Delivery Status: Success\n");
			break;
		case XBAPI_DELIVERY_STATUS_MAC_ACK_FAIL:
			printf("Delivery Status: MAC ACK Fail\n");
			break;
		case XBAPI_DELIVERY_STATUS_CCA_FAIL:
			printf("Delivery Status: CCA Fail\n");
			break;
		case XBAPI_DELIVERY_STATUS_INVALID_DEST:
			printf("Delivery Status: Invalid Destination\n");
			break;
		case XBAPI_DELIVERY_STATUS_NET_ACK_FAIL:
			printf("Delivery Status: Network ACK Fail\n");
			break;
		case XBAPI_DELIVERY_STATUS_NOT_JOINED:
			printf("Delivery Status: Not Joined\n");
			break;
		case XBAPI_DELIVERY_STATUS_SELF_ADDRESSED:
			printf("Delivery Status: Self Addressed\n");
			break;
		case XBAPI_DELIVERY_STATUS_ADDRESS_NOT_FOUND:
			printf("Delivery Status: Address Not Found\n");
			break;
		case XBAPI_DELIVERY_STATUS_ROUTE_NOT_FOUND:
			printf("Delivery Status: Route Not Found\n");
			break;
		case XBAPI_DELIVERY_STATUS_NO_RELAY:
			printf("Delivery Status: No Relay\n");
			break;
		case XBAPI_DELIVERY_STATUS_INVALID_BIND:
			printf("Delivery Status: Invalid Bind Index\n");
			break;
		case XBAPI_DELIVERY_STATUS_RESOURCE_1:
			printf("Delivery Status: Not Enough Resources\n");
			break;
		case XBAPI_DELIVERY_STATUS_BROADCAST_APS:
			printf("Delivery Status: Broadcast APS\n");
			break;
		case XBAPI_DELIVERY_STATUS_UNICAST_APS:
			printf("Delivery Status: UNICAST APS\n");
			break;
		case XBAPI_DELIVERY_STATUS_RESOURCE_2:
			printf("Delivery Status: Not Enough Resources\n");
			break;
		case XBAPI_DELIVERY_STATUS_TOO_LARGE:
			printf("Delivery Status: Too Large\n");
			break;
		case XBAPI_DELIVERY_STATUS_INDIRECT:
			printf("Delivery Status: Indirect\n");
			break;
		case XBAPI_DELIVERY_STATUS_INVALID:
			printf("Delivery Status: Invalid\n");
			break;
	}
	switch(status->discovery_status) {
		case XBAPI_DISCOVERY_STATUS_NONE:
			printf("Discovery Status: No Overhead\n");
			break;
		case XBAPI_DISCOVERY_STATUS_ADDRESS:
			printf("Discovery Status: Address Discovery\n");
			break;
		case XBAPI_DISCOVERY_STATUS_ROUTE:
			printf("Discovery Status: Route Discovery\n");
			break;
		case XBAPI_DISCOVERY_STATUS_BOTH:
			printf("Discovery Status: Address and Route Discovery\n");
			break;
		case XBAPI_DISCOVERY_STATUS_TIMEOUT:
			printf("Discovery Status: Timeout Discovery\n");
			break;
		case XBAPI_DISCOVERY_STATUS_INVALID:
			printf("Discovery Status: Invalid\n");
			break;
	}
	printf("\n");
#else
	(void)status;
#endif
}

void handle_received_packet(xbapi_rx_packet_t *packet, void *user_data) {
#ifdef DEBUG
	printf("Source Address: %llX\n", packet->source_address);
	printf("Source Network Address: %X\n", packet->source_network_address);
	switch(packet->options) {
	case XBAPI_RX_OPTIONS_ACK:
		printf("Receive Options: Acknowledged\n");
		break;
	case XBAPI_RX_OPTIONS_BROADCAST:
		printf("Receive Options: Broadcasted\n");
		break;
	case XBAPI_RX_OPTIONS_ENCRYPTED:
		printf("Receive Options: Encrypted\n");
		break;
	case XBAPI_RX_OPTIONS_END_DEVICE:
		printf("Receive Options: End Device\n");
		break;
	case XBAPI_RX_OPTIONS_INVALID:
		printf("Receive Options: Invalid\n");
		break;
	}
	printf("Data: ");
	for (size_t i = 0; i < talloc_array_length(packet->data); i++)
		printf("0x%02X ", packet->data[i]);
	printf("\n\n");
#endif

	root_node_t *channels = (root_node_t *)user_data;
	node_t *channel_node = find_channel_by_id(channels, packet->source_address);
	if (channel_node != NULL) {
		for (node_t *client = ((channel_t *)channel_node->data)->subscribers->head; client != NULL; client = client->next) {
			size_t data_len = talloc_array_length(packet->data);
			uint8_t *data = packet->data;
			ssize_t ret;
			do {
				ret = send(((connection_t *)client->data)->h_socket, data, data_len, 0);
				if (ret < 0) {
					perror("send()");
					break;
				} else if ((size_t)ret < data_len) {
					data_len -= ret;
					data += ret;
				}
			} while ((size_t)ret < data_len);
		}
	}
}

void handle_modem_changed(xbapi_modem_status_e status, void *user_data) {
	(void)user_data;
#ifdef DEBUG
	switch (status) {
		case XBAPI_MODEM_HARDWARE_RESET:
			printf("Modem Changed: Hardware Reset\n");
			break;
		case XBAPI_MODEM_WDT_RESET:
			printf("Modem Changed: WDT Reset\n");
			break;
		case XBAPI_MODEM_JOINED_NETWORK:
			printf("Modem Changed: Joined Network\n");
			break;
		case XBAPI_MODEM_DISASSOCIATED:
			printf("Modem Changed: Disassociated\n");
			break;
		case XBAPI_MODEM_COORDINATOR_STARTED:
			printf("Modem Changed: Coordinator Started\n");
			break;
		case XBAPI_MODEM_SECURITY_KEY_UPDATED:
			printf("Modem Changed: Key Updated\n");
			break;
		case XBAPI_MODEM_OVERVOLTAGE:
			printf("Modem Changed: Overvoltage\n");
			break;
		case XBAPI_MODEM_CONFIG_CHANGED_WHILE_JOINING:
			printf("Modem Changed: Config Changed While Joining Networks\n");
			break;
		case XBAPI_MODEM_STACK_ERROR:
			printf("Modem Changed: Stack Error\n");
			break;
		case XBAPI_MODEM_STATUS_UNKNOWN:
			printf("Modem Changed: Unknown\n");
			break;
	}
	printf("\n");
#else
	(void)status;
#endif
}

bool handle_operation_completed(xbapi_op_t *op, void *user_data) {
	(void)user_data;
#ifdef DEBUG
	printf("Operation completed\n\n");
#endif
	op_data_t *data = user_data_from_operation(op);

	if (data != NULL) {
		send_response(data->conn->h_socket, data->msgid, NULL);
		remove_operation_from_connection(data->conn, op);
	}

	return true;
}

