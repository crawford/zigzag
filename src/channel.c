#include "channel.h"
#include <stdlib.h>
#include <string.h>

channel_t *create_channel(int id) {
	channel_t *channel = malloc(sizeof(channel_t));
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

void destroy_channel(channel_t *channel) {
	while (channel->subscribers->head != NULL) {
		destroy_node(channel->subscribers, channel->subscribers->head);
	}
	free(channel->subscribers);
	free(channel);
}

