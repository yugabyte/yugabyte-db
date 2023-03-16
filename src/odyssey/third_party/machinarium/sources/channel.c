
/*
 * machinarium.
 *
 * cooperative multitasking engine.
 */

#include <machinarium.h>
#include <machinarium_private.h>

void mm_channel_init(mm_channel_t *channel)
{
	channel->type.is_shared = 1;
	mm_sleeplock_init(&channel->lock);

	mm_list_init(&channel->msg_list);
	channel->msg_list_count = 0;

	mm_list_init(&channel->readers);
	channel->readers_count = 0;
}

void mm_channel_free(mm_channel_t *channel)
{
	if (channel->msg_list_count == 0)
		return;
	mm_list_t *i, *n;
	mm_list_foreach_safe(&channel->msg_list, i, n)
	{
		mm_msg_t *msg = mm_container_of(i, mm_msg_t, link);
		mm_msg_unref(&mm_self->msg_cache, msg);
	}
}

mm_retcode_t mm_channel_write(mm_channel_t *channel, mm_msg_t *msg)
{
	mm_sleeplock_lock(&channel->lock);

	if (channel->readers_count) {
		mm_channelrd_t *reader;
		reader = mm_container_of(channel->readers.next, mm_channelrd_t,
					 link);
		reader->result = msg;
		mm_list_unlink(&reader->link);
		channel->readers_count--;
		int event_mgr_fd;
		event_mgr_fd = mm_eventmgr_signal(&reader->event);
		mm_sleeplock_unlock(&channel->lock);
		if (event_mgr_fd > 0)
			mm_eventmgr_wakeup(event_mgr_fd);
		return MM_OK_RETCODE;
	}

	switch (channel->limit_policy) {
	case MM_CHANNEL_UNLIMITED:
		break;
	case MM_CHANNEL_LIMIT_HARD: {
		if (channel->msg_list_count >= channel->chan_limit) {
			machine_msg_free((machine_msg_t *)msg);
			mm_sleeplock_unlock(&channel->lock);
			return MM_NOTOK_RETCODE;
		}
	} break;
	case MM_CHANNEL_LIMIT_SOFT: {
		// probability of not accepting message is 0 when channel->msg_list_count < channel->chan_limit
		// probability of not accepting message is 1 when channel->msg_list_count >= 2 * channel->chan_limit
		// else uniform distribution probability
		//
		// X || (Y && Z) and eval is lazy
		if ((channel->msg_list_count >= 2 * channel->chan_limit) ||
		    ((channel->msg_list_count >= channel->chan_limit) &&
		     (machine_lrand48() % channel->chan_limit <
		      channel->msg_list_count - channel->chan_limit))) {
			machine_msg_free((machine_msg_t *)msg);
			mm_sleeplock_unlock(&channel->lock);
			return MM_NOTOK_RETCODE;
		}
	} break;
	default:
		assert(0);
	}

	mm_list_append(&channel->msg_list, &msg->link);
	channel->msg_list_count++;

	mm_sleeplock_unlock(&channel->lock);
	return MM_OK_RETCODE;
}

mm_msg_t *mm_channel_read(mm_channel_t *channel, uint32_t time_ms)
{
	/* try to get first message, if no other readers are
	 * waiting, otherwise put reader in the wait
	 * channel */
	mm_sleeplock_lock(&channel->lock);

	mm_list_t *next;
	if ((channel->msg_list_count > 0) && (channel->readers_count == 0)) {
		next = mm_list_pop(&channel->msg_list);
		channel->msg_list_count--;
		mm_sleeplock_unlock(&channel->lock);
		return mm_container_of(next, mm_msg_t, link);
	}

	/* put reader into channel and register event */
	mm_channelrd_t reader;
	reader.result = NULL;
	mm_list_init(&reader.link);
	mm_eventmgr_add(&mm_self->event_mgr, &reader.event);

	mm_list_append(&channel->readers, &reader.link);
	channel->readers_count++;

	mm_sleeplock_unlock(&channel->lock);

	/* wait for cancel, timedout or writer event */
	mm_eventmgr_wait(&mm_self->event_mgr, &reader.event, time_ms);

	mm_sleeplock_lock(&channel->lock);

	if (!reader.result) {
		assert(channel->readers_count > 0);
		channel->readers_count--;
		mm_list_unlink(&reader.link);
	}

	mm_sleeplock_unlock(&channel->lock);

	/* timedout or cancel */
	if (reader.event.call.status != 0) {
		if (reader.result)
			mm_msg_unref(&mm_self->msg_cache, reader.result);
		return NULL;
	}

	return reader.result;
}
