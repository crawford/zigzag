#include <stdlib.h>
#include "client_node.h"

client_node *create_node(client_root *root) {
	client_node *new = malloc(sizeof(client_node));
	new->next = NULL;
	new->prev = root->tail;

	if (root->tail) {
		root->tail->next = new;
	}
	root->tail = new;

	if (root->head == NULL) {
		root->head = new;
	}

	root->count++;

	return new;
}

void destroy_node_by_node(client_root *root, client_node *node) {
	if (node == NULL) {
		return;
	}

	if (node != root->head && node != root->tail) {
		node->next->prev = node->prev;
		node->prev->next = node->next;
	} else {
		if (node == root->head) {
			root->head = node->next;
			if (root->head) {
				root->head->prev = NULL;
			}
		}

		if (node == root->tail) {
			root->tail = node->prev;
			if (root->tail) {
				root->tail->next = NULL;
			}
		}
	}

	root->count--;

	free(node);
}

void destroy_node_by_fd(client_root *root, int fd) {
	client_node *node = root->head;
	while (node) {
		if (node->h_socket == fd) {
			destroy_node_by_node(root, node);
			return;
		}
		node = node->next;
	}
}

