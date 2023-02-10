
/*
 * Odyssey.
 *
 * Scalable PostgreSQL connection pooler.
 */

#include <stdlib.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#include <machinarium.h>
#include <odyssey_test.h>

extern void machinarium_test_init(void);
extern void machinarium_test_create0(void);
extern void machinarium_test_create1(void);
extern void machinarium_test_config(void);
extern void machinarium_test_context_switch(void);
extern void machinarium_test_sleep(void);
extern void machinarium_test_sleep_random(void);
extern void machinarium_test_sleep_yield(void);
extern void machinarium_test_sleep_cancel0(void);
extern void machinarium_test_join(void);
extern void machinarium_test_condition0(void);
extern void machinarium_test_eventfd0(void);
extern void machinarium_test_stat(void);
extern void machinarium_test_signal0(void);
extern void machinarium_test_signal1(void);
extern void machinarium_test_signal2(void);
extern void machinarium_test_channel_create(void);
extern void machinarium_test_channel_rw0(void);
extern void machinarium_test_channel_rw1(void);
extern void machinarium_test_channel_rw2(void);
extern void machinarium_test_channel_rw3(void);
extern void machinarium_test_channel_rw4(void);
extern void machinarium_test_channel_timeout(void);
extern void machinarium_test_channel_cancel(void);
extern void machinarium_test_channel_shared_create(void);
extern void machinarium_test_channel_shared_rw0(void);
extern void machinarium_test_channel_shared_rw1(void);
extern void machinarium_test_channel_shared_rw2(void);
extern void machinarium_test_sleeplock(void);
extern void machinarium_test_producer_consumer0(void);
extern void machinarium_test_producer_consumer1(void);
extern void machinarium_test_producer_consumer2(void);
extern void machinarium_test_io_new(void);
extern void machinarium_test_connect(void);
extern void machinarium_test_connect_timeout(void);
extern void machinarium_test_connect_cancel0(void);
extern void machinarium_test_connect_cancel1(void);
extern void machinarium_test_accept_timeout(void);
extern void machinarium_test_accept_cancel(void);
extern void machinarium_test_getaddrinfo0(void);
extern void machinarium_test_getaddrinfo1(void);
extern void machinarium_test_getaddrinfo2(void);
extern void machinarium_test_client_server0(void);
extern void machinarium_test_client_server1(void);
extern void machinarium_test_client_server2(void);
extern void machinarium_test_client_server_unix_socket(void);
extern void machinarium_test_read_10mb0(void);
extern void machinarium_test_read_10mb1(void);
extern void machinarium_test_read_10mb2(void);
extern void machinarium_test_read_timeout(void);
extern void machinarium_test_read_cancel(void);
extern void machinarium_test_read_var(void);
extern void machinarium_test_tls0(void);
extern void machinarium_test_tls_unix_socket(void);
extern void machinarium_test_tls_read_10mb0(void);
extern void machinarium_test_tls_read_10mb1(void);
extern void machinarium_test_tls_read_10mb2(void);
extern void machinarium_test_tls_read_multithread(void);
extern void machinarium_test_tls_read_var(void);

extern void odyssey_test_tdigest(void);
extern void odyssey_test_attribute(void);
extern void odyssey_test_util(void);
extern void odyssey_test_lock(void);
extern void odyssey_test_hba(void);

int main(int argc, char *argv[])
{
	(void)argc;
	(void)argv;
	odyssey_test(machinarium_test_init);
	odyssey_test(machinarium_test_create0);
	odyssey_test(machinarium_test_create1);
	odyssey_test(machinarium_test_config);
	odyssey_test(machinarium_test_context_switch);
	odyssey_test(machinarium_test_sleep);
	odyssey_test(machinarium_test_sleep_random);
	odyssey_test(machinarium_test_sleep_yield);
	odyssey_test(machinarium_test_sleep_cancel0);
	odyssey_test(machinarium_test_join);
	odyssey_test(machinarium_test_condition0);
	odyssey_test(machinarium_test_eventfd0);
	odyssey_test(machinarium_test_stat);
	odyssey_test(machinarium_test_signal0);
	odyssey_test(machinarium_test_signal1);
	odyssey_test(machinarium_test_signal2);
	odyssey_test(machinarium_test_channel_create);
	odyssey_test(machinarium_test_channel_rw0);
	odyssey_test(machinarium_test_channel_rw1);
	odyssey_test(machinarium_test_channel_rw2);
	odyssey_test(machinarium_test_channel_rw3);
	odyssey_test(machinarium_test_channel_rw4);
	odyssey_test(machinarium_test_channel_timeout);
	odyssey_test(machinarium_test_channel_cancel);
	odyssey_test(machinarium_test_channel_shared_create);
	odyssey_test(machinarium_test_channel_shared_rw0);
	odyssey_test(machinarium_test_channel_shared_rw1);
	odyssey_test(machinarium_test_channel_shared_rw2);
	odyssey_test(machinarium_test_sleeplock);
	odyssey_test(machinarium_test_producer_consumer0);
	odyssey_test(machinarium_test_producer_consumer1);
	odyssey_test(machinarium_test_producer_consumer2);
	odyssey_test(machinarium_test_io_new);
	odyssey_test(machinarium_test_connect);
	odyssey_test(machinarium_test_connect_timeout);
	odyssey_test(machinarium_test_connect_cancel0);
	odyssey_test(machinarium_test_connect_cancel1);
	odyssey_test(machinarium_test_accept_timeout);
	odyssey_test(machinarium_test_accept_cancel);
	odyssey_test(machinarium_test_getaddrinfo0);
	odyssey_test(machinarium_test_getaddrinfo1);
	odyssey_test(machinarium_test_getaddrinfo2);
	odyssey_test(machinarium_test_client_server0);
	odyssey_test(machinarium_test_client_server1);
	odyssey_test(machinarium_test_client_server2);
	odyssey_test(machinarium_test_client_server_unix_socket);
	odyssey_test(machinarium_test_read_10mb0);
	odyssey_test(machinarium_test_read_10mb1);
	odyssey_test(machinarium_test_read_10mb2);
	odyssey_test(machinarium_test_read_timeout);
	odyssey_test(machinarium_test_read_cancel);
	odyssey_test(machinarium_test_read_var);
	odyssey_test(machinarium_test_tls0);
	odyssey_test(machinarium_test_tls_unix_socket);
	odyssey_test(machinarium_test_tls_read_10mb0);
	odyssey_test(machinarium_test_tls_read_10mb1);
	odyssey_test(machinarium_test_tls_read_10mb2);
	odyssey_test(machinarium_test_tls_read_multithread);
	odyssey_test(machinarium_test_tls_read_var);
	odyssey_test(odyssey_test_tdigest);
	odyssey_test(odyssey_test_attribute);
	odyssey_test(odyssey_test_util);
	odyssey_test(odyssey_test_lock);
	odyssey_test(odyssey_test_hba);

	return 0;
}