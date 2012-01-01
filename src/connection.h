#ifndef CONNECTION_H
#define CONNECTION_H

#include "node.h"
#include "xbapi.h"

typedef struct {
	int h_socket;
	root_node_t *pending_operations;
} connection_t;

bool add_operation_to_connection(connection_t *, xbapi_op_t *);
void remove_operation_from_connection(connection_t *, xbapi_op_t *);
void destroy_connection(connection_t *);

#endif
