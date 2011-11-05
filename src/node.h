#ifndef NODE_H
#define NODE_H

typedef struct node_ {
	struct node_ *next;
	struct node_ *prev;
	void *data;
} node;

typedef struct {
	node *head;
	node *tail;
	int count;
} node_root;

node *create_node(node_root *);
void destroy_node(node_root *, node *);

#endif

