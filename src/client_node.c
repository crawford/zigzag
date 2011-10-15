#include <stdlib.h>
#include "client_node.h"

client_node *create_node(client_root *root) {
	client_node *new = malloc(sizeof(client_node));
	new->next = NULL;
	new->prev = root->tail;

	if (root->tail) {
		root->tail->next = new;
	} else {
		root->head = new;
		root->tail = new;
	}

	root->count++;

	return new;
}

void destroy_node(client_root *root, client_node *node) {
	if (node == NULL) {
		return;
	}

	if (node == root->head) {
		root->head = NULL;
		root->tail = NULL;
	} else {
		node->prev->next = node->next;
	}

	if (node == root->tail) {
		root->tail = node->prev;
	} else {
		node->next->prev = node->prev;
	}

	root->count--;

	free(node);
}

