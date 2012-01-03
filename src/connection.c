#include <stdlib.h>
#include "connection.h"

bool add_operation_to_connection(connection_t *conn, xbapi_op_t *op) {
	node_t *node = create_node(conn->pending_operations);
	if (node == NULL) return false;

	node->data = op;
	return true;
}

void remove_operation_from_connection(connection_t *conn, xbapi_op_t *op) {
	node_t *next = conn->pending_operations->head;
	while (next != NULL) {
		node_t *cur = next;
		next = cur->next;

		if (cur->data == op) {
			destroy_node(conn->pending_operations, cur);
		}
	}
}

void destroy_connection(connection_t *conn) {
	node_t *node = conn->pending_operations->head;
	while (node != NULL) {
		node_t *cur = node;
		node = node->next;
		free(cur);
	}
	free(conn->pending_operations);
}

