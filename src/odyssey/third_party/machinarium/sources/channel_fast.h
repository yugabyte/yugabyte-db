#ifndef MM_CHANNEL_FAST_H
#define MM_CHANNEL_FAST_H

/*
 * machinarium.
 *
 * cooperative multitasking engine.
 */

/*
 * Channel fast is machinarium implementation
 * of machine channel interface
 * */

typedef struct mm_channelfast_rd mm_channelfast_rd_t;
typedef struct mm_channelfast mm_channelfast_t;

struct mm_channelfast_rd {
	mm_call_t call;
	int signaled;
	mm_list_t link;
};

struct mm_channelfast {
	mm_channeltype_t type;
	mm_list_t incoming;
	int incoming_count;
	mm_list_t readers;
	int readers_count;
};

void mm_channelfast_init(mm_channelfast_t *);
void mm_channelfast_free(mm_channelfast_t *);
mm_retcode_t mm_channelfast_write(mm_channelfast_t *, mm_msg_t *);

mm_msg_t *mm_channelfast_read(mm_channelfast_t *, uint32_t);

#endif /* MM_CHANNEL_FAST_H */
