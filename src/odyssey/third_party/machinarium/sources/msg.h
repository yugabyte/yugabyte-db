#ifndef MM_MSG_H
#define MM_MSG_H

/*
 * machinarium.
 *
 * cooperative multitasking engine.
 */

typedef struct mm_msg mm_msg_t;

struct mm_msg {
	uint16_t refs;
	uint64_t machine_id;
	int type;
	mm_buf_t data;
	mm_list_t link;
};

static inline void mm_msg_init(mm_msg_t *msg, int type)
{
	msg->refs = 0;
	msg->type = type;
	msg->machine_id = 0;
	mm_buf_init(&msg->data);
	mm_list_init(&msg->link);
}

#endif /* MM_MSG_H */
