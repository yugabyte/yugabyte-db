---
title: >
  Case study: PL/pgSQL procedures for provisioning roles with privileges on only the current database [YSQL]
headerTitle: >
  Case study: PL/pgSQL procedures for provisioning roles with privileges on only the current database
linkTitle: >
  Case study: PL/pgSQL procedures-for role provisioning
description: >
  Case study: describes the use of PL/pgSQL procedures for provisioning roles with privileges on only the current database. [YSQL].
menu:
  stable:
    identifier: provisioning-roles-for-current-database
    parent: language-plpgsql-subprograms
    weight: 30
type: docs
showRightNav: true
---

## The scenario—role provisioning

One significant reason to use PL/pgSQL subprograms, rather than simply to let client-side code issue top-level SQL, is to implement an API for an overall application's database functionality and to hide all of the implementation details behind that API so that client-side code that connects as a dedicated _client_ role:

- can execute the subprograms that jointly define the API
- cannot even see, let alone use, other artifacts like tables and various "helper" subprograms that the API subprograms call

Indeed, the self-evident value of this approach is one of the main reasons that the developers of various RDBMSs, in the late 1980s and the early 1990s, invested the not inconsiderable effort that it took to implement support for user-defined subprograms.

This scheme is easily implemented by distributing the ownership of the application's database objects among several roles, by distributing their locations among several schemas, and by granting privileges only where these are needed.

This approach is sometimes referred to as a "hard shell" encapsulation of the database functionality. A self-contained, fully tested, working code example of the scheme is included in the **[ysql-case-studies](https://github.com/YugabyteDB-Samples/ysql-case-studies)** GitHub repository. Look for the **[hard-shell](https://github.com/YugabyteDB-Samples/ysql-case-studies/tree/main/hard-shell/README.md)** case study. It takes just seconds to download and unzip the entire repository and to find the _hard-shell_ directory at the top level. It has its own _README_.

The present section illustrates an approach that is used throughout the _ysql-case-studies_ code corpus for all of the case studies and for the multitenancy infrastructure that hosts them:

- **Use PL/pgSQL _security definer_ subprograms to encapsulate role-provisioning SQL statements so that their effect can be constrained to conform to carefully designed rules of practice.**

## The role-provisioning paradigm

Role provisioning is informed by these critical rules of practice:

- Databases are conventionally named _d0_, _d1_, ..._d42_,... and so on. (The names are never used in code. They're used only at session creation time.)
- So-called _local roles_ are allowed to connect only to _exactly one_ database and optionally to own objects only in that database.
- Local roles are systematically named like this: _d42$mgr_, _d42$data_, _d42$code_, _d42$api, d42$client_, and so on. This convention ensures that the choice of the information-bearing part of the names, the so-called nicknames (_mgr_, _data_, _code_, _api_, and _client_ in the present example), can be freely chosen without any risk of collision with local roles for other databases.
- The idea is this:
  - The database has no _public_ schema. The _connect_ privilege on this database is granted to all of the local roles; and the _create_ privilege on this database is granted to all of the local roles _except for the client role_.
  - The _data_ role owns tables, indexes, and where appropriate, other closely related objects like triggers and domains. The _data_ role grants usage on the schema(s) where its objects are located—together with appropriate privileges (like _select_, _insert_, _update_, and _delete_ on tables) to the _code_ role.
  - The _code_ role owns the _security definer_ user-defined subprograms that implement the application's functionality. The _code_ role grants usage on the schema(s) where those of its subprograms that explicitly expose the API (but not their helper subprograms) are located—together with _execute_ on those subprograms to the _api_ role.
  - The _api_ role owns _security definer_ subprograms that do nothing more than act as invocation shims for those _code_-owned subprograms that expose the API. This pass-through scheme ensures that the intention is explicit (i.e. that the helpers are invisible to roles that invoke the _api_-owned subprograms. The _api_ role grants usage on the schema(s) where its API-defining subprograms are located—together with _execute_ on those subprograms, to the _client_ role.
  - The _client_ role is the only role whose name and password is known to the engineers who implement client-side code. And the _client_ role owns no schema.
  - This privilege and owner scheme has the outcome that client-side sessions that connect as the _client_ role cannot create new objects and cannot change any of the objects that implement the overall application's database functionality. Such sessions, by construction, can do nothing more than invoke the _api_-owned subprograms that implement the designed functionality. Notice that they simply cannot make unintended changes to the content of the  _data_-owned tables.

An example of the use of this scheme is demonstrated in the section **[Using the "hard-shell" approach to separate the code that opens a cursor from the code that fetches the rows](../plpgsql-syntax-and-semantics/executable-section/basic-statements/cursor-manipulation/#using-the-hard-shell-approach-to-separate-the-code-that-opens-a-cursor-from-the-code-that-fetches-the-rows)**.

Notice that, according to the larger design aims, yet more roles that just these might be needed. And, of course, the might all have names which better reflect the overall purpose that do these generic role names.

## The code

Use a suitable playground cluster. First, create a sandbox database with a non-colliding name that follows the convention, say _d42_:

```plpgsql
\c yugabyte yugabyte
set client_min_messages = warning;

\set db                     d42
drop    database if exists  :db;
create  database            :db;
```

Notice, in the code that follows, that the name _d42_ is never used. Rather, because the database name is needed only at "connect" time, the _psql_ meta-command \\_c_ to create a session can use the _psql_ variable _db_. This means that if the name that you first consider happens to collide with the name of an existing database, or if you just prefer another name like _d17_, then you can simply replace it in the single point of definition of the _psql_ variable _db_ of definition.

Next, connect to that database and create the "manager" role as a local role for it with no special attributes and with a name that follows the convention—_d42$mgr_:

```plpgsql
\c :db yugabyte
set client_min_messages = warning;
drop schema public;

do $body$
declare
  mgr_name constant text not null := current_database()||'$mgr';
begin
  begin
    execute format('drop owned by %I',                                             mgr_name);
  exception when undefined_object then null;
  end;
  execute format('drop role if exists %I',                                         mgr_name);
  execute format('create role %I login password $$m$$ ',                           mgr_name);
  execute format('grant connect, create on database %I to %I', current_database(), mgr_name);
  execute format('create schema mgr authorization %I',                             mgr_name);
end;
$body$;

\set quoted_db '\'':db'\''
select :quoted_db||'$mgr' as mgr
\gset
```

Notice how the _psql_ variable _db_ is used to define the _psql_ variable _mgr_ to express the name _d42$mgr_. This technique completes the practice that allows the entire corpus of _.sql_ scripts that jointly install an application's database backend can be insulated from sensitivity to the database's name.

Using PL/pgSQL in a [_do_ statement](../../../the-sql-language/statements/cmd_do/) to create and configure the manager role for database _d42_ brings these advantages:

- The role name of the manager for database _d42_, _d42$mgr_, is declared as _mgr_name_ by prepending the name that the _current_database()_ built-in function returns to the nickname _mgr_, separating the two components of the name with the _$_ sign.

- _mgr_name_ is then re-used _five_ times in these SQL statements: _drop owned by_, _drop role_, _create role_, grant connect, create on database_, and _create schema_.

- The _current_database()_ function is used also to ensure that _d42$mgr_ is a local role for the database _d42_.

- The fact that the  _drop owned by_ statement must be used before the  _drop role_ statement, and the fact that _drop owned by_ statement has no _if exists_ syntax can be accommodated so that role provisioning, first dropping the to-be-created role, always completes silently. This is achieved by implementing the _drop owned by_ statement within a tightly enclosing block statement that has a handler for the _undefined_object_ (_"role does not exist"_) exception.

Next, set up the "manager" role to be able to provision ordinary roles in the database for which it's a local role. Remain connected as the superuser _yugabyte_ in order to create the _security definer_ procedure _mgr.re_create_role()_ that will allow _d42$mgr_ to provision local roles for the database _d0_. Notice that _d42$mgr_ doesn't have the authority directly to use the SQL statements that this needs. This perfectly illustrates the value of _security definer_ subprograms.

Because provisioned local roles might need the ability to revoke or grant privileges on objects that they own, the _security invoker_ procedures _revoke_all_from_public()_ and _mgr.grant_priv()_ are created first.

```plpgsql
create procedure mgr.revoke_all_from_public(
  object_kind  in text,
  object       in text)
  security invoker
  set search_path = pg_catalog, pg_temp
  language plpgsql
as $body$
declare
  kind   constant text not null := case
                                     when lower(object_kind) = 'view' then 'table'
                                     else                                   object_kind
                                   end;
begin
  execute format('revoke all on %s %s from public', kind, object);
end;
$body$;
call mgr.revoke_all_from_public('procedure', 'mgr.revoke_all_from_public');

create procedure mgr.grant_priv(
  priv              in text,
  object_kind       in text,
  object            in text,
  grantee_nickname  in text)
  security invoker
  set search_path = pg_catalog, pg_temp
  language plpgsql
as $body$
declare
  kind   constant text not null := case
                                     when lower(object_kind) = 'view' then 'table'
                                     else                                   object_kind
                                   end;
  r_name constant text not null := case
                                     when grantee_nickname = 'public' then 'public'
                                     else current_database()||'$'||grantee_nickname
                                   end;
begin
  execute format('grant %s on %s %s to %I', priv, kind, object, r_name);
end;
$body$;
call mgr.revoke_all_from_public('procedure', 'mgr.grant_priv');

create procedure mgr.re_create_role(nickname in text, passwd in text, cr_on_db in boolean)
  set search_path = pg_catalog, pg_temp
  security definer
  language plpgsql
as $body$
declare
   r_name constant text not null := current_database()||'$'||nickname;
begin
  begin
    execute format('drop owned by %I', r_name);
  exception when undefined_object then null;
  end;
  execute format('drop role if exists %I', r_name);
  execute format($$create role %I login password '%s'$$, r_name, passwd);
  execute format('grant connect on database %I to %I', current_database(), r_name);
  if cr_on_db then
    execute format('grant create on database %I to %I', current_database(), r_name);
  end if;
  call mgr.grant_priv('usage',   'schema',    'mgr',                        nickname);
  call mgr.grant_priv('execute', 'procedure', 'mgr.revoke_all_from_public', nickname);
  call mgr.grant_priv('execute', 'procedure', 'mgr.grant_priv',             nickname);
end;
$body$;

call mgr.revoke_all_from_public('procedure', 'mgr.re_create_role');
call mgr.grant_priv('execute', 'procedure', 'mgr.re_create_role', 'mgr');
```

Finally, create four local roles to support a minimal demonstration of the _hard shell_ approach:

```plpgsql
\c :db :mgr
set client_min_messages = warning;

call mgr.re_create_role('data',   'd', true);
call mgr.re_create_role('code',   'c', true);
call mgr.re_create_role('api',    'a', true);
call mgr.re_create_role('client', 'k', false);
```
