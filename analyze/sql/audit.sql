-- Stop on error
\set ON_ERROR_STOP on

-- Make sure that errors are detected and not automatically rolled back
\set ON_ERROR_ROLLBACK off

-- Create pgaudit extension
CREATE EXTENSION IF NOT EXISTS pgaudit;

-- Create a function to check for role exists
create function pg_temp.role_exists
(
    role_name text
)
    returns boolean as $$
begin
    return
    (
        select count(*) = 1
          from pg_roles
         where rolname = role_name
    );
end
$$ language plpgsql security definer;

-- Create cluster-wide roles if they do no exist yet
do $$
begin
    if not pg_temp.role_exists('pgaudit_owner') then
        create role pgaudit_owner;
    end if;

    if not pg_temp.role_exists('pgaudit_etl') then
        create role pgaudit_etl;
    end if;

    if not pg_temp.role_exists('pgaudit') then
        create user pgaudit in role pgaudit_etl;
    end if;
end $$;

-- Create pgaudit schema
create schema pgaudit authorization pgaudit_owner;

-- Set session authorization so all schema objects are owned by pgaudit_owner
set session authorization pgaudit_owner;

-- Create usage on schema to public
grant usage
   on schema pgaudit
   to public;

-- Create session table to track user database sessions
create table pgaudit.session
(
    session_id text not null,
    process_id int not null,
    session_start_time timestamp with time zone not null,
    user_name text not null,
    application_name text,
    connection_from text,
    state text not null
        constraint session_state_ck check (state in ('ok', 'error')),

    constraint session_pk
        primary key (session_id)
);

grant select,
      insert,
      update (application_name)
   on pgaudit.session
   to pgaudit_etl;

-- Create logon table to track recent user login info
create table pgaudit.logon
(
     user_name text not null,
     last_success timestamp with time zone,
     current_success timestamp with time zone,
     last_failure timestamp with time zone,
     failures_since_last_success int not null,

     constraint logon_pk
        primary key (user_name)
);

grant select,
      insert (user_name, current_success, last_failure, failures_since_last_success),
      update (last_success, current_success, last_failure, failures_since_last_success)
   on pgaudit.logon
   to pgaudit_etl;

-- Create logon_info() function to allow unprivileged users to get (only) their logon info
create or replace function pgaudit.logon_info()
    returns table
(
    last_success timestamp with time zone,
    last_failure timestamp with time zone,
    failures_since_last_success int
)
    as $$
begin
    return query
    (
        select logon.last_success,
               logon.last_failure,
               logon.failures_since_last_success
          from pgaudit.logon
         where logon.user_name = session_user
    );
end
$$ language plpgsql security definer;

grant execute on function pgaudit.logon_info() to public;

-- Create log_event table to track all events logged to the PostgreSQL log
create table pgaudit.log_event
(
    session_id text not null
        constraint logevent_sessionid_fk
            references pgaudit.session (session_id),
    session_line_num numeric not null,
    log_time timestamp(3) with time zone not null,
    command text,
    error_severity text,
    sql_state_code text,
    virtual_transaction_id text,
    transaction_id bigint,
    message text,
    detail text,
    hint text,
    query text,
    query_pos integer,
    internal_query text,
    internal_query_pos integer,
    context text,
    location text,

    constraint logevent_pk
        primary key (session_id, session_line_num)
);

grant select,
      insert
   on pgaudit.log_event
   to pgaudit_etl;

-- Create audit_statment table to track all user statements logged by the pgaudit extension
create table pgaudit.audit_statement
(
    session_id text not null
        constraint auditstatement_sessionid_fk
            references pgaudit.session (session_id),
    statement_id numeric not null,
    state text not null default 'ok'
        constraint auditstatement_state_ck check (state in ('ok', 'error')),
    error_session_line_num numeric,

    constraint auditstatement_pk
        primary key (session_id, statement_id),
    constraint auditstatement_sessionid_sessionlinenum_fk
        foreign key (session_id, error_session_line_num)
        references pgaudit.log_event (session_id, session_line_num)
        deferrable initially deferred
);

grant select,
      update (state, error_session_line_num),
      insert
   on pgaudit.audit_statement
   to pgaudit_etl;

-- Create audit_statment table to track all user sub-statements logged by the pgaudit extension
create table pgaudit.audit_substatement
(
    session_id text not null,
    statement_id numeric not null,
    substatement_id numeric not null,
    substatement text,
    parameter text[],

    constraint auditsubstatement_pk
        primary key (session_id, statement_id, substatement_id),
    constraint auditsubstatement_sessionid_statementid_fk
        foreign key (session_id, statement_id)
        references pgaudit.audit_statement (session_id, statement_id)
);

grant select,
      insert
   on pgaudit.audit_substatement
   to pgaudit_etl;

-- Create audit_statment table to track all user sub-statement detail logged by the pgaudit extension
create table pgaudit.audit_substatement_detail
(
    session_id text not null,
    statement_id numeric not null,
    substatement_id numeric not null,
    session_line_num numeric not null,
    audit_type text not null
        constraint auditsubstatementdetail_audittype_ck
            check (audit_type in ('session', 'object')),
    class text not null,
    command text not null,
    object_type text,
    object_name text,

    constraint auditsubstatementdetail_pk
        primary key (session_id, statement_id, substatement_id, session_line_num),
    constraint auditsubstatementdetail_sessionid_sessionlinenum_unq
        unique (session_id, session_line_num),
    constraint auditsubstatementdetail_sessionid_statementid_substatementid_fk
        foreign key (session_id, statement_id, substatement_id)
        references pgaudit.audit_substatement (session_id, statement_id, substatement_id),
    constraint auditsubstatementdetail_sessionid_sessionlinenum_fk
        foreign key (session_id, session_line_num)
        references pgaudit.log_event (session_id, session_line_num)
        deferrable initially deferred
);

grant select,
      insert
   on pgaudit.audit_substatement_detail
   to pgaudit_etl;

-- Create vw_audit_event view to allow easy access to the pgaudit log entries
create view pgaudit.vw_audit_event as
select session.session_id,
       log_event.session_line_num,
       log_event.log_time,
       session.user_name,
       audit_statement.statement_id,
       audit_statement.state,
       audit_statement.error_session_line_num,
       audit_substatement.substatement_id,
       audit_substatement.substatement,
       audit_substatement_detail.audit_type,
       audit_substatement_detail.class,
       audit_substatement_detail.command,
       audit_substatement_detail.object_type,
       audit_substatement_detail.object_name
  from pgaudit.audit_substatement_detail
       inner join pgaudit.log_event
            on log_event.session_id = audit_substatement_detail.session_id
           and log_event.session_line_num = audit_substatement_detail.session_line_num
       inner join pgaudit.session
            on session.session_id = audit_substatement_detail.session_id
       inner join pgaudit.audit_substatement
            on audit_substatement.session_id = audit_substatement_detail.session_id
           and audit_substatement.statement_id = audit_substatement_detail.statement_id
           and audit_substatement.substatement_id = audit_substatement_detail.substatement_id
       inner join pgaudit.audit_statement
            on audit_statement.session_id = audit_substatement_detail.session_id
           and audit_statement.statement_id = audit_substatement_detail.statement_id;
