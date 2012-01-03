#ifndef NODE_H
#define NODE_H

typedef struct _node {
	struct _node *next;
	struct _node *prev;
	void *data;
} node_t;

typedef struct {
	node_t *head;
	node_t *tail;
	int count;
} root_node_t;

node_t *create_node(root_node_t *);
void destroy_node(root_node_t *, node_t *);

#endif

