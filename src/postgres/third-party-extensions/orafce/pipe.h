#ifndef __PIPE__
#define __PIPE__

#define LOCALMSGSZ (8*1024)
#define SHMEMMSGSZ (30*1024)
#define MAX_PIPES  30
#define MAX_EVENTS 30
#define MAX_LOCKS  256

typedef struct _message_item {
	char *message;
	float8 timestamp;
	struct _message_item *next_message;
	struct _message_item *prev_message;
	unsigned char message_id;
	int *receivers;                     /* copy of array all registered receivers */
	int receivers_number;
} message_item;

typedef struct _message_echo {
	struct _message_item *message;
	unsigned char message_id;
	struct _message_echo *next_echo;
} message_echo;

typedef struct {
	char *event_name;
	unsigned char max_receivers;
	int *receivers;
	int receivers_number;
	struct _message_item *messages;
} alert_event;

typedef struct {
	int sid;
	message_echo *echo;
} alert_lock;

bool ora_lock_shmem(size_t size, int max_pipes, int max_events, int max_locks, bool reset);

#define ERRCODE_ORA_PACKAGES_LOCK_REQUEST_ERROR        MAKE_SQLSTATE('3','0', '0','0','1')

#define LOCK_ERROR() \
	ereport(ERROR, \
	(errcode(ERRCODE_ORA_PACKAGES_LOCK_REQUEST_ERROR), \
	 errmsg("lock request error"), \
	 errdetail("Failed exclusive locking of shared memory."), \
	 errhint("Restart PostgreSQL server.")));
#endif
