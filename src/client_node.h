#ifndef CLIENT_NODE_H
#define CLIENT_NODE_H

typedef struct client_node_ {
	struct client_node_ *next;
	struct client_node_ *prev;
	int h_socket;
} client_node;

typedef struct {
	client_node *head;
	client_node *tail;
	int count;
} client_root;

client_node *create_node(client_root *root);
void destroy_node(client_root *root, client_node *node);

#endif

