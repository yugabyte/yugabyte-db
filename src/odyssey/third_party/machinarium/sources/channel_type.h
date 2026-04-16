#ifndef MM_CHANNEL_TYPE_H
#define MM_CHANNEL_TYPE_H

/*
 * machinarium.
 *
 * cooperative multitasking engine.
 */

typedef struct mm_channeltype mm_channeltype_t;

struct mm_channeltype {
	int is_shared;
} __attribute__((packed));

#endif /* MM_CHANNEL_TYPE_H */
