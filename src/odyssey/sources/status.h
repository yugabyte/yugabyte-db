#ifndef ODYSSEY_STATUS_H
#define ODYSSEY_STATUS_H

/*
 * Odyssey.
 *
 * Scalable PostgreSQL connection pooler.
 */

typedef enum {
	OD_UNDEF,
	OD_OK,
	OD_SKIP,
	OD_ATTACH,
	OD_DETACH,
	OD_WAIT_SYNC,
	OD_READ_FULL,
	OD_STOP,
	OD_EOOM,
	OD_EATTACH,
	OD_EATTACH_TOO_MANY_CONNECTIONS,
	OD_ESERVER_CONNECT,
	OD_ESERVER_READ,
	OD_ESERVER_WRITE,
	OD_ECLIENT_READ,
	OD_ECLIENT_WRITE,
	OD_ESYNC_BROKEN,
	OD_ECATCHUP_TIMEOUT,
	YB_OD_DEPLOY_ERR,
} od_frontend_status_t;

static inline char *od_frontend_status_to_str(od_frontend_status_t status)
{
	switch (status) {
	case OD_UNDEF:
		return "OD_UNDEF";
	case OD_OK:
		return "OD_OK";
	case OD_SKIP:
		return "OD_SKIP";
	case OD_ATTACH:
		return "OD_UNDEF";
	case OD_DETACH:
		return "OD_DETACH";
	case OD_WAIT_SYNC:
		return "OD_WAIT_SYNC";
	case OD_STOP:
		return "OD_STOP";
	case OD_EOOM:
		return "OD_EOOM";
	case OD_READ_FULL:
		return "OD_READ_FULL";
	case OD_EATTACH:
		return "OD_EATTACH";
	case OD_EATTACH_TOO_MANY_CONNECTIONS:
		return "OD_EATTACH_TOO_MANY_CONNECTIONS";
	case OD_ESERVER_CONNECT:
		return "OD_ESERVER_CONNECT";
	case OD_ESERVER_READ:
		return "OD_ESERVER_READ";
	case OD_ESERVER_WRITE:
		return "OD_ESERVER_WRITE";
	case OD_ECLIENT_READ:
		return "OD_ECLIENT_READ";
	case OD_ECLIENT_WRITE:
		return "OD_ECLIENT_WRITE";
	case OD_ESYNC_BROKEN:
		return "OD_ESYNC_BROKEN";
	case OD_ECATCHUP_TIMEOUT:
		return "OD_ECATCHUP_TIMEOUT";
	case YB_OD_DEPLOY_ERR:
		return "YB_OD_DEPLOY_ERR";
	}
	return "UNKNOWN";
}

static const od_frontend_status_t od_frontend_status_errs[] = {
	OD_EOOM,
	OD_EATTACH,
	OD_EATTACH_TOO_MANY_CONNECTIONS,
	OD_ESERVER_CONNECT,
	OD_ESERVER_READ,
	OD_ESERVER_WRITE,
	OD_ECLIENT_WRITE,
	OD_ECLIENT_READ,
	OD_ESYNC_BROKEN,
	OD_ECATCHUP_TIMEOUT,
	YB_OD_DEPLOY_ERR,
};

#define OD_FRONTEND_STATUS_ERRORS_TYPES_COUNT \
	sizeof(od_frontend_status_errs) / sizeof(od_frontend_status_errs[0])

static inline bool od_frontend_status_is_err(od_frontend_status_t status)
{
	for (size_t i = 0; i < OD_FRONTEND_STATUS_ERRORS_TYPES_COUNT; ++i) {
		if (od_frontend_status_errs[i] == status) {
			return true;
		}
	}
	return false;
}

typedef enum {
	OD_ROUTER_OK,
	OD_ROUTER_ERROR,
	OD_ROUTER_ERROR_NOT_FOUND,
	OD_ROUTER_ERROR_LIMIT,
	OD_ROUTER_ERROR_LIMIT_ROUTE,
	OD_ROUTER_ERROR_TIMEDOUT,
	OD_ROUTER_ERROR_REPLICATION,
	OD_ROUTER_INSUFFICIENT_ACCESS,
} od_router_status_t;

static inline char *od_router_status_to_str(od_router_status_t status)
{
	switch (status) {
	case OD_ROUTER_OK:
		return "OD_ROUTER_OK";
	case OD_ROUTER_ERROR:
		return "OD_ROUTER_ERROR";
	case OD_ROUTER_ERROR_NOT_FOUND:
		return "OD_ROUTER_ERROR_NOT_FOUND";
	case OD_ROUTER_ERROR_LIMIT:
		return "OD_ROUTER_ERROR_LIMIT";
	case OD_ROUTER_ERROR_LIMIT_ROUTE:
		return "OD_ROUTER_ERROR_LIMIT_ROUTE";
	case OD_ROUTER_ERROR_TIMEDOUT:
		return "OD_ROUTER_ERROR_TIMEDOUT";
	case OD_ROUTER_ERROR_REPLICATION:
		return "OD_ROUTER_ERROR_REPLICATION";
	default:
		return "unkonown";
	}
}

static const od_router_status_t od_router_status_errs[] = {
	OD_ROUTER_ERROR,	  OD_ROUTER_ERROR_NOT_FOUND,
	OD_ROUTER_ERROR_LIMIT,	  OD_ROUTER_ERROR_LIMIT_ROUTE,
	OD_ROUTER_ERROR_TIMEDOUT, OD_ROUTER_ERROR_REPLICATION
};

#define OD_ROUTER_STATUS_ERRORS_TYPES_COUNT \
	sizeof(od_router_status_errs) / sizeof(od_router_status_errs[0])

/* errors that could be counted per route */
static const od_router_status_t od_router_route_status_errs[] = {
	OD_ROUTER_ERROR_LIMIT_ROUTE,
};

#define OD_ROUTER_ROUTE_STATUS_ERRORS_TYPES_COUNT \
	sizeof(od_router_route_status_errs) /     \
		sizeof(od_router_route_status_errs[0])

static inline bool od_router_status_is_err(od_router_status_t status)
{
	for (size_t i = 0; i < OD_ROUTER_STATUS_ERRORS_TYPES_COUNT; ++i) {
		if (od_router_status_errs[i] == status) {
			return true;
		}
	}
	return false;
}

#endif /* ODYSSEY_STATUS_H */
