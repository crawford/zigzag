#include "channel.h"
#include <stdlib.h>
#include <string.h>

channel *create_channel(int id) {
	channel *channel = malloc(sizeof(channel));
	if (channel == NULL)
		return NULL;

	channel->id = id;
	channel->subscribers = malloc(sizeof(node_root));
	if (channel->subscribers == NULL) {
		free(channel);
		return NULL;
	}
	memset(channel->subscribers, 0, sizeof(node_root));
	return channel;
}

void destroy_channel(channel *channel) {
	while (channel->subscribers->head != NULL) {
		destroy_node(channel->subscribers, channel->subscribers->head);
	}
	free(channel->subscribers);
	free(channel);
}

