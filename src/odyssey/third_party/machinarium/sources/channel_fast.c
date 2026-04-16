
/*
 * machinarium.
 *
 * cooperative multitasking engine.
 */

#include <machinarium.h>
#include <machinarium_private.h>

void mm_channelfast_init(mm_channelfast_t *channel)
{
	channel->type.is_shared = 0;
	mm_list_init(&channel->incoming);
	channel->incoming_count = 0;
	mm_list_init(&channel->readers);
	channel->readers_count = 0;
}

void mm_channelfast_free(mm_channelfast_t *channel)
{
	mm_list_t *i, *n;
	mm_list_foreach_safe(&channel->incoming, i, n)
	{
		mm_msg_t *msg;
		msg = mm_container_of(i, mm_msg_t, link);
		mm_msg_unref(&mm_self->msg_cache, msg);
	}
}

mm_retcode_t mm_channelfast_write(mm_channelfast_t *channel, mm_msg_t *msg)
{
	mm_errno_set(0);
	mm_list_append(&channel->incoming, &msg->link);
	channel->incoming_count++;

	if (!channel->readers_count) {
		return MM_OK_RETCODE;
	}

	/* remove first reader from the queue to properly
	 * handle other waiters on next invocation */
	mm_list_t *first;
	first = channel->readers.next;
	mm_channelfast_rd_t *reader;
	reader = mm_container_of(first, mm_channelfast_rd_t, link);
	reader->signaled = 1;

	mm_list_unlink(&reader->link);
	channel->readers_count--;

	mm_scheduler_wakeup(&mm_self->scheduler, reader->call.coroutine);
	return MM_OK_RETCODE;
}

mm_msg_t *mm_channelfast_read(mm_channelfast_t *channel, uint32_t time_ms)
{
	mm_errno_set(0);
	while (channel->incoming_count == 0) {
		mm_channelfast_rd_t reader;
		reader.signaled = 0;
		mm_list_init(&reader.link);

		mm_list_append(&channel->readers, &reader.link);
		channel->readers_count++;

		mm_call(&reader.call, MM_CALL_CHANNEL, time_ms);
		if (reader.call.status != 0) {
			/* timedout or cancel */
			if (!reader.signaled) {
				assert(channel->readers_count > 0);
				channel->readers_count--;
				mm_list_unlink(&reader.link);
			}
			return NULL;
		}
		assert(reader.signaled);
	}

	mm_list_t *first;
	first = mm_list_pop(&channel->incoming);
	channel->incoming_count--;
	return mm_container_of(first, mm_msg_t, link);
}
