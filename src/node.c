#include <stdlib.h>
#include "node.h"

node_t *create_node(root_node_t *root) {
	node_t *new = malloc(sizeof(node_t));
	if (new == NULL)
		return NULL;

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

void destroy_node(root_node_t *root, node_t *node) {
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

