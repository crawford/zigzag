#ifndef CHANNEL_H
#define CHANNEL_H

#include "node.h"

typedef struct {
	int id;
	node_root *subscribers;
} channel;

channel *create_channel(int id);
void destroy_channel(channel *channel);

#endif

