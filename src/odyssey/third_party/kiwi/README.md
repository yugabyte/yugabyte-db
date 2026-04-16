**Kiwi**

PostgreSQL protocol-level C library.

Library is designed to provide most of the functionality needed to write or read
[PostgreSQL protocol messages](https://www.postgresql.org/docs/9.6/static/protocol.html).
Both Frontend (client to server) and Backend (server to client) messages are supported, making
it possible to write client or server simulation applications.

No network part is supported. Only buffer management and packet validation.

Library is intedend to work in pair with the machinarium framework.

**PostgreSQL packet readers**

```C
/* Read initial message (StartupMessage, CancelRequest, SSLRequest) */
kiwi_read_startup()

/* Read any other PostgreSQL packet */
kiwi_read()
```

**FRONTEND**

**Write messages to Backend**

```C
/* StartupMessage */
kiwi_fe_write_startup_message()

/* CancelRequest */
kiwi_fe_write_cancel()

/* SSLRequest */
kiwi_fe_write_ssl_request()

/* Terminate */
kiwi_fe_write_terminate()

/* PasswordMessage */
kiwi_fe_write_password()

/* Query */
kiwi_fe_write_query()

/* Query for prep stmt */
kiwi_fe_write_prep_stmt()

/* Parse */
kiwi_fe_write_parse()

/* Bind */
kiwi_fe_write_bind()

/* Describe */
kiwi_fe_write_describe();

/* Execute */
kiwi_fe_write_execute();

/* Sync */
kiwi_fe_write_sync();
```

**Read messages from Backend**

```C
/* ReadyForQuery */
kiwi_fe_read_ready();

/* BackendKeyData */
kiwi_fe_read_key();

/* Authentication messages */
kiwi_fe_read_auth();

/* ParameterStatus */
kiwi_fe_read_parameter();

/* ErrorResponse */
kiwi_fe_read_error();
```

**BACKEND**

**Write messages to Frontend**

```C
/* ErrorResponse */
kiwi_be_write_error()
kiwi_be_write_error_fatal()
kiwi_be_write_error_panic()

/* NoticeResponse */
kiwi_be_write_notice()

/* AuthenticationOk */
kiwi_be_write_authentication_ok()

/* AuthenticationCleartextPassword */
kiwi_be_write_authentication_clear_text()

/* AuthenticationMD5Password */
kiwi_be_write_authentication_md5()

/* BackendKeyData */
kiwi_be_write_backend_key_data()

/* ParameterStatus */
kiwi_be_write_parameter_status()

/* EmptyQueryResponse */
kiwi_be_write_empty_query()

/* CommandComplete */
kiwi_be_write_complete()

/* ReadyForQuery */
kiwi_be_write_ready()

/* ParseComplete */
kiwi_be_write_parse_complete()

/* BindComplete */
kiwi_be_write_bind_complete()

/* PortalSuspended */
kiwi_be_write_portal_suspended()

/* NoData */
kiwi_be_write_no_data()

/* RowDescription */
kiwi_be_write_row_description()
kiwi_be_write_row_description_add()

/* DataRow */
kiwi_be_write_data_row()
kiwi_be_write_data_row_add()
```

**Read messages from Frontend**

```C
/* Read StartupMessage, CancelRequest or SSLRequest */
kiwi_be_read_startup();

/* PasswordMessage */
kiwi_be_read_password();
```
