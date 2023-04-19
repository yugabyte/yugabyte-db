#ifndef MACHINARIUM_PRIVATE_H_
#define MACHINARIUM_PRIVATE_H_

/*
 * machinarium.
 *
 * cooperative multitasking engine.
 */

#define _GNU_SOURCE 1

#ifndef IOV_MAX
#define IOV_MAX __IOV_MAX
#endif

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <time.h>
#include <assert.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/uio.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/signalfd.h>

#include <openssl/opensslv.h>
#include <openssl/ssl.h>
#include <openssl/crypto.h>
#include <openssl/engine.h>
#include <openssl/conf.h>
#include <openssl/bio.h>
#include <openssl/err.h>

#include "build.h"
#include "macro.h"
#include "util.h"
#include "sleep_lock.h"
#include "list.h"
#include "buf.h"

#include "fd.h"
#include "poll.h"
#include "timer.h"
#include "clock.h"
#include "idle.h"
#include "loop.h"
#include "epoll.h"
#include "socket.h"
#include "bind.h"

#include "context_stack.h"
#include "context.h"
#include "coroutine.h"
#include "coroutine_cache.h"
#include "scheduler.h"
#include "call.h"

#include "thread.h"
#include "signal_mgr.h"

#include "cond.h"
#include "event.h"
#include "event_mgr.h"

#include "msg.h"
#include "msg_cache.h"
#include "channel_type.h"
#include "channel_limit.h"
#include "channel.h"
#include "channel_fast.h"

#include "task.h"
#include "task_mgr.h"

#include "machine.h"
#include "machine_mgr.h"
#include "mm.h"

#include "iov.h"
#include "io.h"
#include "tls.h"
#include "compression.h"
#include "zpq_stream.h"

#include "lrand48.h"

#endif
