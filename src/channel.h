#ifndef CHANNEL_H
#define CHANNEL_H

#include <stdint.h>
#include "node.h"

typedef struct {
	uint64_t id;
	root_node_t *subscribers;
} channel_t;

channel_t *create_channel(uint64_t id);
void destroy_channel(channel_t *channel);

#endif

