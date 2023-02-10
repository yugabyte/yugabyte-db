#ifndef MM_CHANNEL_H
#define MM_CHANNEL_H

/*
 * machinarium.
 *
 * cooperative multitasking engine.
 */

typedef struct mm_channelrd mm_channelrd_t;
typedef struct mm_channel mm_channel_t;

struct mm_channelrd {
	mm_event_t event;
	mm_msg_t *result;
	mm_list_t link;
};

struct mm_channel {
	mm_channeltype_t type;
	mm_sleeplock_t lock;
	mm_list_t msg_list;
	int msg_list_count;
	mm_list_t readers;
	int readers_count;
	int chan_limit;
	mm_channel_limit_policy_t limit_policy;
};

void mm_channel_init(mm_channel_t *);
void mm_channel_free(mm_channel_t *);
mm_retcode_t mm_channel_write(mm_channel_t *, mm_msg_t *);

mm_msg_t *mm_channel_read(mm_channel_t *, uint32_t);

#endif /* MM_CHANNEL_H */
