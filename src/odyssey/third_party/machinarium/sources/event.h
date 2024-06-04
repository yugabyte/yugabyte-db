#ifndef MM_EVENT_H
#define MM_EVENT_H

/*
 * machinarium.
 *
 * cooperative multitasking engine.
 */

typedef struct mm_event mm_event_t;

typedef enum {
	MM_EVENT_NONE,
	MM_EVENT_WAIT,
	MM_EVENT_READY,
	MM_EVENT_ACTIVE
} mm_eventstate_t;

struct mm_event {
	mm_eventstate_t state;
	mm_call_t call;
	void *event_mgr;
	mm_list_t link;
};

#endif /* MM_EVENT_H */
