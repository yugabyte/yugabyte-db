#ifndef KIWI_HEADER_H
#define KIWI_HEADER_H

/*
 * kiwi.
 *
 * postgreSQL protocol interaction library.
 */

typedef struct kiwi_header kiwi_header_t;

typedef enum {
	KIWI_FE_TERMINATE = 'X',
	KIWI_FE_PASSWORD_MESSAGE = 'p',
	KIWI_FE_QUERY = 'Q',
	KIWI_FE_FUNCTION_CALL = 'F',
	KIWI_FE_PARSE = 'P',
	KIWI_FE_BIND = 'B',
	KIWI_FE_DESCRIBE = 'D',
	KIWI_FE_EXECUTE = 'E',
	KIWI_FE_SYNC = 'S',
	KIWI_FE_CLOSE = 'C',
	KIWI_FE_COPY_DATA = 'd',
	KIWI_FE_COPY_DONE = 'c',
	KIWI_FE_COPY_FAIL = 'f',
	KIWI_FE_AUTH = 'A',
	KIWI_FE_SET_SESSION_PARAMETER = 's'
} kiwi_fe_type_t;

typedef enum {
	KIWI_FE_CLOSE_PREPARED_STATEMENT = 'S',
	KIWI_FE_CLOSE_PORTAL = 'P',
} kiwi_fe_close_type_t;

typedef enum {
	KIWI_FE_DESCRIBE_PREPARED_STATEMENT = 'S',
	KIWI_FE_DESCRIBE_PORTAL = 'P',
} kiwi_fe_describe_type_t;

typedef enum {
	KIWI_BE_AUTHENTICATION = 'R',
	KIWI_BE_BACKEND_KEY_DATA = 'K',
	KIWI_BE_PARSE_COMPLETE = '1',
	KIWI_BE_BIND_COMPLETE = '2',
	KIWI_BE_CLOSE_COMPLETE = '3',
	KIWI_BE_COMMAND_COMPLETE = 'C',
	KIWI_BE_COPY_IN_RESPONSE = 'G',
	KIWI_BE_COPY_OUT_RESPONSE = 'H',
	KIWI_BE_COPY_BOTH_RESPONSE = 'W',
	KIWI_BE_COPY_DATA = 'd',
	KIWI_BE_COPY_DONE = 'c',
	KIWI_BE_COPY_FAIL = 'f',
	KIWI_BE_DATA_ROW = 'D',
	KIWI_BE_EMPTY_QUERY_RESPONSE = 'I',
	KIWI_BE_ERROR_RESPONSE = 'E',
	KIWI_BE_FUNCTION_CALL_RESPONSE = 'V',
	KIWI_BE_NEGOTIATE_PROTOCOL_VERSION = 'v',
	KIWI_BE_NO_DATA = 'n',
	KIWI_BE_NOTICE_RESPONSE = 'N',
	KIWI_BE_NOTIFICATION_RESPONSE = 'A',
	KIWI_BE_PARAMETER_DESCRIPTION = 't',
	KIWI_BE_PARAMETER_STATUS = 'S',
	KIWI_BE_PORTAL_SUSPENDED = 's',
	KIWI_BE_READY_FOR_QUERY = 'Z',
	KIWI_BE_ROW_DESCRIPTION = 'T',
	KIWI_BE_COMPRESSION = 'z',
	YB_KIWI_BE_FATAL_FOR_LOGICAL_CONNECTION = 'F',
	YB_OID_DETAILS = 'O',
} kiwi_be_type_t;

struct kiwi_header {
	uint8_t type;
	uint32_t len;
} __attribute__((packed));

static inline char *kiwi_header_data(kiwi_header_t *header)
{
	return (char *)header + sizeof(kiwi_header_t);
}

static inline char *kiwi_fe_type_to_string(int type)
{
	switch (type) {
	case KIWI_FE_TERMINATE:
		return "Terminate";
	case KIWI_FE_PASSWORD_MESSAGE:
		return "PasswordMessage";
	case KIWI_FE_QUERY:
		return "Query";
	case KIWI_FE_FUNCTION_CALL:
		return "FunctionCall";
	case KIWI_FE_PARSE:
		return "Parse";
	case KIWI_FE_BIND:
		return "Bind";
	case KIWI_FE_DESCRIBE:
		return "Describe";
	case KIWI_FE_EXECUTE:
		return "Execute";
	case KIWI_FE_SYNC:
		return "Sync";
	case KIWI_FE_CLOSE:
		return "Close";
	case KIWI_FE_COPY_DATA:
		return "CopyData";
	case KIWI_FE_COPY_DONE:
		return "CopyDone";
	case KIWI_FE_COPY_FAIL:
		return "CopyFail";
	}
	return "Unknown";
}

static inline char *kiwi_be_type_to_string(int type)
{
	switch (type) {
	case KIWI_BE_AUTHENTICATION:
		return "Authentication";
	case KIWI_BE_BACKEND_KEY_DATA:
		return "BackendKeyData";
	case KIWI_BE_PARSE_COMPLETE:
		return "ParseComplete";
	case KIWI_BE_BIND_COMPLETE:
		return "BindComplete";
	case KIWI_BE_CLOSE_COMPLETE:
		return "CloseComplete";
	case KIWI_BE_COMMAND_COMPLETE:
		return "CommandComplete";
	case KIWI_BE_COPY_IN_RESPONSE:
		return "CopyInResponse";
	case KIWI_BE_COPY_OUT_RESPONSE:
		return "CopyOutResponse";
	case KIWI_BE_COPY_BOTH_RESPONSE:
		return "CopyBothResponse";
	case KIWI_BE_COPY_DATA:
		return "CopyData";
	case KIWI_BE_COPY_DONE:
		return "CopyDone";
	case KIWI_BE_COPY_FAIL:
		return "CopyFail";
	case KIWI_BE_DATA_ROW:
		return "DataRow";
	case KIWI_BE_EMPTY_QUERY_RESPONSE:
		return "EmptyQueryResponse";
	case KIWI_BE_ERROR_RESPONSE:
		return "ErrorResponse";
	case KIWI_BE_FUNCTION_CALL_RESPONSE:
		return "FunctionCallResponse";
	case KIWI_BE_NEGOTIATE_PROTOCOL_VERSION:
		return "NegotiateProtocolVersion";
	case KIWI_BE_NO_DATA:
		return "NoData";
	case KIWI_BE_NOTICE_RESPONSE:
		return "NoticeResponse";
	case KIWI_BE_NOTIFICATION_RESPONSE:
		return "NotificationResponse";
	case KIWI_BE_PARAMETER_DESCRIPTION:
		return "ParameterDescription";
	case KIWI_BE_PARAMETER_STATUS:
		return "ParameterStatus";
	case KIWI_BE_PORTAL_SUSPENDED:
		return "PortalSuspended";
	case KIWI_BE_READY_FOR_QUERY:
		return "ReadyForQuery";
	case KIWI_BE_ROW_DESCRIPTION:
		return "RowDescription";
	case YB_KIWI_BE_FATAL_FOR_LOGICAL_CONNECTION:
		return "FatalForLogicalConnection";
	case YB_OID_DETAILS:
		return "OidDetails";
	}
	return "Unknown";
}

#endif /* KIWI_HEADER_H */
