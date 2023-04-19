
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
#include <math.h>
#include <unistd.h>
#include <sys/time.h>
#include <time.h>

#include <machinarium.h>
#include <kiwi.h>
#include <sources/readahead.h>
#include <sources/io.h>

#include "histogram.h"

typedef struct {
	int id;
	od_io_t io;
	int coroutine_id;
	int processed;
} stress_client_t;

typedef struct {
	char *dbname;
	char *user;
	char *host;
	char *port;
	int time_to_run;
	int clients;
} stress_t;

static stress_t stress;
static od_histogram_t stress_histogram;
static int stress_run;

static inline void stress_client_main(void *arg)
{
	stress_client_t *client = arg;

	/* create client io */
	od_io_prepare(&client->io, machine_io_create(), 8192);
	if (client->io.io == NULL) {
		printf("client %d: failed to create io\n", client->id);
		return;
	}

	machine_set_nodelay(client->io.io, 1);
	machine_set_keepalive(client->io.io, 1, 7200, 75, 9, 0);

	/* resolve host */
	struct addrinfo *ai = NULL;
	int rc;
	rc = machine_getaddrinfo(stress.host, stress.port, NULL, &ai,
				 UINT32_MAX);
	if (rc == -1) {
		printf("client %d: failed to resolve host\n", client->id);
		return;
	}

	/* connect */
	rc = machine_connect(client->io.io, ai->ai_addr, UINT32_MAX);
	freeaddrinfo(ai);
	if (rc == -1) {
		printf("client %d: failed to connect\n", client->id);
		return;
	}

	printf("client %d: connected\n", client->id);

	/* handle client startup */
	kiwi_fe_arg_t argv[] = { { "user", 5 },
				 { stress.user, strlen(stress.user) + 1 },
				 { "database", 9 },
				 { stress.dbname, strlen(stress.dbname) + 1 } };

	machine_msg_t *msg;
	msg = kiwi_fe_write_startup_message(NULL, 4, argv);
	if (msg == NULL)
		return;

	rc = od_write(&client->io, msg);
	if (rc == -1) {
		printf("client %d: write error: %s\n", client->id,
		       machine_error(client->io.io));
		return;
	}

	rc = machine_write_stop(client->io.io);
	if (rc == -1) {
		printf("client %d: write error: %s\n", client->id,
		       machine_error(client->io.io));
		return;
	}

	while (1) {
		msg = od_read(&client->io, UINT32_MAX);
		if (msg == NULL) {
			printf("read error");
			return;
		}
		kiwi_be_type_t type = *(char *)machine_msg_data(msg);

		if (type == KIWI_BE_ERROR_RESPONSE) {
			printf("Error response: %s\n",
			       (char *)machine_msg_data(msg) + 5);
			machine_msg_free(msg);
			return;
		}
		machine_msg_free(msg);

		if (type == KIWI_BE_READY_FOR_QUERY)
			break;
	}

	printf("client %d: ready\n", client->id);

	char query[] = "select generate_series(1,10,1)";

	/* oltp */
	while (stress_run) {
		int start_time = od_histogram_time_us();

		/* request */
		msg = kiwi_fe_write_query(NULL, query, sizeof(query));
		if (msg == NULL)
			return;
		rc = od_write(&client->io, msg);
		if (rc == -1) {
			printf("client %d: write error: %s\n", client->id,
			       machine_error(client->io.io));
			return;
		}
		/* no flush */

		/* reply */
		for (;;) {
			msg = od_read(&client->io, INT32_MAX);
			if (msg == NULL) {
				printf("client %d: read error: %s\n",
				       client->id,
				       machine_error(client->io.io));
				return;
			}
			char type = *(char *)machine_msg_data(msg);
			machine_msg_free(msg);

			if (type == KIWI_BE_ERROR_RESPONSE)
				break;

			if (type == KIWI_BE_READY_FOR_QUERY) {
				int execution_time =
					od_histogram_time_us() - start_time;
				od_histogram_add(&stress_histogram,
						 execution_time);
				client->processed++;
				break;
			}
		}
	}

	/* finish */
	msg = kiwi_fe_write_terminate(NULL);
	if (msg == NULL)
		return;
	rc = od_write(&client->io, msg);
	if (rc == -1) {
		printf("client %d: write error: %s\n", client->id,
		       machine_error(client->io.io));
		return;
	}

	machine_close(client->io.io);
	printf("client %d: done (%d processed)\n", client->id,
	       client->processed);
}

static inline void stress_main(void *arg)
{
	stress_t *stress = arg;

	stress_client_t *clients;
	clients = calloc(stress->clients, sizeof(stress_client_t));
	if (clients == NULL)
		return;

	stress_run = 1;

	/* create workers */
	int i = 0;
	for (; i < stress->clients; i++) {
		stress_client_t *client = &clients[i];
		client->id = i;
		client->coroutine_id =
			machine_coroutine_create(stress_client_main, client);
	}

	/* give time for work */
	machine_sleep(stress->time_to_run * 1000);

	stress_run = 0;

	/* wait for completion and calculate stats */
	for (i = 0; i < stress->clients; i++) {
		stress_client_t *client = &clients[i];
		machine_join(client->coroutine_id);
		if (client->io.io)
			machine_io_free(client->io.io);
	}
	free(clients);

	/* result */
	od_histogram_print(&stress_histogram, stress->clients,
			   stress->time_to_run);
}

int main(int argc, char *argv[])
{
	od_histogram_init(&stress_histogram);
	memset(&stress, 0, sizeof(stress));
	stress_run = 0;
	char *user = getenv("USER");
	if (user == NULL)
		user = "test";
	stress.user = user;
	stress.dbname = user;
	stress.host = "localhost";
	stress.port = "6432";
	stress.time_to_run = 5;
	stress.clients = 10;

	int opt;
	while ((opt = getopt(argc, argv, "d:u:h:p:t:c:")) != -1) {
		switch (opt) {
		/* database */
		case 'd':
			stress.dbname = optarg;
			break;
			/* user */
		case 'u':
			stress.user = optarg;
			break;
			/* host */
		case 'h':
			stress.host = optarg;
			break;
			/* port */
		case 'p':
			stress.port = optarg;
			break;
			/* time */
		case 't':
			stress.time_to_run = atoi(optarg);
			break;
			/* clients */
		case 'c':
			stress.clients = atoi(optarg);
			break;
		default:
			printf("PostgreSQL benchmarking.\n\n");
			printf("usage: %s [duhptc]\n", argv[0]);
			printf("  \n");
			printf("  -d <database>   database name\n");
			printf("  -u <user>       user name\n");
			printf("  -h <host>       server address\n");
			printf("  -p <port>       server port\n");
			printf("  -t <time>       time to run (seconds)\n");
			printf("  -c <clients>    number of clients\n");
			return 1;
		}
	}

	printf("PostgreSQL benchmarking.\n\n");
	printf("time to run: %d secs\n", stress.time_to_run);
	printf("clients:     %d\n", stress.clients);
	printf("database:    %s\n", stress.dbname);
	printf("user:        %s\n", stress.user);
	printf("host:        %s\n", stress.host);
	printf("port:        %s\n", stress.port);
	printf("\n");

	machinarium_init();

	int64_t machine;
	machine = machine_create("stresser", stress_main, &stress);

	int rc = machine_wait(machine);

	machinarium_free();
	return rc;
}
