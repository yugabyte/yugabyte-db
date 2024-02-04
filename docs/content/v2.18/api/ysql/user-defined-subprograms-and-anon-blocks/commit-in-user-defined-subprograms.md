---
title: Issuing "commit" in user-defined subprograms and anonymous blocks [YSQL]
headerTitle: Issuing "commit" in user-defined subprograms and anonymous blocks
linkTitle: «Commit» in user-defined subprograms
description: Explains why you should should avoid issuing "commit" in user-defined subprograms and anonymous blocks [YSQL]
menu:
  v2.18:
    identifier: commit-in-user-defined-subprograms
    parent: user-defined-subprograms-and-anon-blocks
    weight: 5
type: docs
---

This section states the restrictions that govern the use of "commit" and other transaction control statements in user-defined subprograms (and, by extension, in anonymous blocks). It then makes a practical recommendation always to invoke a user-defined subprogram using _single statement automatic transaction mode_. See these two sections:

- [Semantics of issuing non-transaction-control SQL statements during an ongoing transaction](../../txn-model-for-top-level-sql/#semantics-of-issuing-non-transaction-control-sql-statements-during-an-ongoing-transaction)
- [Semantics of issuing non-transaction-control SQL statements when no transaction is ongoing](../../txn-model-for-top-level-sql/#semantics-of-issuing-non-transaction-control-sql-statements-when-no-transaction-is-ongoing).)

Finally, it demonstrates all of the restrictions with code examples.

## Restrictions that govern the use of "commit" in user-defined subprograms

The examples at the end of this section show that "commit" will cause a run-time error under these circumstances:

- In any _language sql_ subprogram—procedure or function.
- In any _language plpgsql_ function.
- In any _language plpgsql_ procedure that has _security definer_.
- In any _language plpgsql_ procedure that has _security invoker_ and whose header includes a _set_ attribute (like, for example, _set search_path = pg_catalog, pg_temp_).
- In any _language plpgsql_ procedure that has _security invoker_ and whose header does not include a _set_ attribute, within the executable section of a block statement (at any level of nesting) that has an _exception_ section.
- In any _language plpgsql_ procedure that has _security invoker_, whose header does not include a _set_ attribute, whose procedural code does not include an _exception_ section, and that is called from the client in _multistatement manual transaction mode_—i.e. within an explicitly issued _begin; ... commit;_ transaction.

(These restrictions apply, in fact, to the use of _any_ transaction control statement.)

Make sure that you have read and understood the section [The transaction model for top-level SQL statements](../../txn-model-for-top-level-sql/). When a user-defined procedure is invoked by a _call_ statement and when a user-defined function is invoked as a term in an expression in any SQL statement that allows this, the invocation, tautologically, is done within the context of an already ongoing transaction:

- _either_ the statement that invokes the subprogram is issued when there is no ongoing transaction—and so this initiates _single statement automatic transaction mode_, which lasts for the duration of the subprogram invocation;
- _or_ the statement that invokes the subprogram is issued when a transaction has been manually started and _multistatement manual transaction mode_ is therefore ongoing.

This holds, too, if the client has turned on its client-side so-called _autocommit_ mode. However, if you follow the recommendation [Simply avoid using any client-side 'autocommit' feature](../../txn-model-for-top-level-sql/#avoid-client-side-autocommit), then you will never have to consider this possibility.

<a name="avoid-invoking-user-defined-subprograms-in-multistatement-manual-txn-mode"></a>
{{< tip title="Avoid invoking user-defined subprograms in multistatement manual transaction mode." >}}
The overwhelmingly common practice is to invoke a user-defined subprogram in single statement automatic transaction mode—and to let the final implicit _commit_ that the system adds look after the persistence (or _rollback_, in the case of error). The point is that these two alternatives:

- _either_ using multistatement manual transaction mode to encapsulate ordinary top-level SQL statements
- _or_ invoking a user-defined subprogram (which might invoke other user-defined subprograms) in single statement automatic transaction mode

both achieve the same thing. Each ultimately invokes several "leaf" SQL statements (i.e. statements that do not invoke user-defined subprograms) within a single transaction—and so using both mechanisms together achieves no more that using just one of them by itself.

If you are convinced that you have a use case for invoking two or more user-defined subprograms, separately, using multistatement manual transaction mode rather than invoking them from a user-defined subprogram dedicated to that purpose, then you should explain the reasoning for this in the design documentation.
{{< /tip >}}

## Code examples that demonstrate the restrictions

To run these tests, connect to a sandbox database as an ordinarily privileged user and start by re-creating and dropping the schema _s_. The examples use fully qualified identifiers to emphasize the point that it's, in general, unsafe to rely on unqualified identifiers in top-level SQL because these are resolved according to the reigning session-level _search_path_ and this is vulnerable to manipulation (innocent or otherwise) by client-side code. See the section [Name resolution within top-level SQL statements](../../name-resolution-in-top-level-sql/). All of the examples rely on a table created thus:

```plpgsql
create table s.t(k serial primary key, v int not null);
```

### All restrictions are met so the 'commit' succeeds

```plpgsql
create procedure s.p_ok(v_in in int)
  security invoker
  language plpgsql
as $body$
begin
  insert into s.t(v) values(v_in);
  commit;
end;
$body$;
call s.p_ok(17);
select k, v from s.t;
```

Both the _create_ and the _call_ statements complete without error and the _select_ produces the expected result:

```output
 k | v  
---+----
 1 | 17
```

Notice, though, that the outcome is indistinguishable from what it would have been _without_ the _commit_. You might see blog posts that recommend implementing a procedure that inserts a huge amount of data from a staging table (with no constraints) into the ultimate destination table which has constraints that dirty data might violate. The idea is that the ingesting procedure would execute a loop that uses a suitably parameterize _between_ predicate to ingest, and _commit_ the data in manageable batches. The thinking is that if dirty input data causes an error, at least the data that has been committed to date is safely ingested. However, this is in general a feeble idea because the procedure can't include an _exception_ section to detect when an error occurs, record that outcome somewhere, and then move on to the next batch. Furthermore, the vastness of the task suggests parallelizing the ingestion. This is easily done with a suitable client-side language like, say, Java that allows multithreading of several database sessions—and here _single statement automatic transaction mode_ using a procedure with formal arguments for the _between_ bounds with an _exception_ section but with no _commit_ meets the requirement nicely.

### Break "p_ok()" by calling it from within an ongoing transaction that the client started

```plpgsql
start transaction;
call s.p_ok(42);
```

This causes the expected _2D000_ error:

```outout
invalid transaction termination
```

Here, like with many of the negative examples, the error message gives you no clue about why your attempt is deemed to be illegal. You need to know the rules in advance by having studied this (or the PostgreSQL) documentation.

### Break "p_ok()" by including an "exception" section in one of its block statements

```plpgsql
drop procedure if exists s.p_bad(int) cascade;
create procedure s.p_bad(v_in in int)
  security invoker
  language plpgsql
as $body$
begin
  insert into s.t(v) values(v_in);
  commit;
exception
  when not_null_violation then
    raise info '"not_null_violation" handled.';
end;
$body$;
call s.p_bad(42);
```

The _create_ statement succeeds but the _call_ statement fails with the _2D000:_ error:

```output
cannot commit while a subtransaction is active
```

Now try this, deliberately to provoke the _not_null_violation_ error:

```plpgsql
call s.p_bad(null::int);
```

This is the outcome:

```output
INFO:  00000: "not_null_violation" handled.
```

just as you'd hoped for this scenario. You get this outcome, of course, because the point of execution never gets to the illegal _commit_ statement. The net effect, though, is that your design concept, here, isn't viable. However, you _can_ get what is presumably the intended behavior simply by omitting the _commit_ statement and by relying on autocommit to do the commit (or to issue _rollback_ if an unhandled error escapes from the _call_). The upshot is that you _are_ able to implement the popular approach that catches and detects exceptions and that:

- _either_ generates a helpful response when the exception is regrettable but nevertheless expected (like _unique_violation_ on inserting a colliding natural key (_"This nickname is taken. Please try another one"_).
- _or_ logs all the context information into an _incidents_ table and returns the autogenerated incident number to the client when the exception is unexpected. Here, any effect that the block statement might have had to date is automatically rolled back when the exception is handled and than autocommit will now commit the new _incidents_ row.) Of course, this paradigm requires that the _exception_ section is placed at the end of the subprogram's outermost block. You can see this approach in action in the ["hard-shell"](https://github.com/YugabyteDB-Samples/ysql-case-studies/tree/main/hard-shell) case study in the file [60-install-api.sql](https://github.com/YugabyteDB-Samples/ysql-case-studies/blob/main/hard-shell/60-install-api.sql)

This limitation _does_ bring one serious practical consequence. Some errors are provoked only by _commit_. The canonical example is a serialization error. You might want to hide all the details of this behind a hard-shell API by implementing a retry loop within the _language plpgsql_ subprogram that does the operation that might cause the serialization error. But this is simply not possible. Rather, you must implement such retry logic in client code.

{{< tip title="YugabyteDB will presently implement retry for serialization errors and similar transparently." >}}
If you have this use-case, you should ask on the [YugabyteDB public Slack channel](https://communityinviter.com/apps/yugabyte-db/register) to find out the current status of the automatic retry feature.
{{< /tip >}}

### Break "p_ok()" by including "set search_path" in its header

Try this:

```plpgsql
drop procedure if exists s.p_bad(int) cascade;
create procedure s.p_bad(v_in in int)
  set search_path = pg_catalog, pg_temp
  security invoker
  language plpgsql
as $body$
begin
  insert into s.t(v) values(v_in);
  commit;
end;
$body$;
call s.p_bad(17);
```

The _create_ statement succeeds. But the _call_ statement fails with the _2D000_ error (_invalid transaction termination_). The point here is that the semantics of _set search_path_ in the header require that the reigning value of the run-time parameter (_search_path_ is just an example here) at the moment before the _call_ must be restored on call completion. The PostgreSQL documentation doesn't explain how the save-and-restore is done. But whatever is the internal mechanism, it would be subverted if the subprogram were to do its own explicit _commit_.

There is a viable workaround: simply use _set local search_path = ..._ (or the equivalent for a different run-time parameter). This has the significant disadvantage that, because it's done with ordinary user-written code rather than declaratively, it's not visible in the _pg_proc.proconfig_ catalog table column. This makes it hard to police a set of subprograms (especially to do this programmatically) to ensure that a particular defined practice rule is followed.

### Break "p_ok()" by changing it to a function

Try this:

```plpgsql
drop function if exists s.f_bad(int) cascade;
create function s.f_bad(v_in in int)
  returns text
  security invoker
  language plpgsql
as $body$
begin
  insert into s.t(v) values(v_in);
  commit;
  return 'success';
end;
$body$;
select s.f_bad(42);
```

The _create_ statement succeeds. But the _select_ statement fails with the _2D000_ error (_invalid transaction termination_).  This arguably represents no practical problem because a function ought not to make data changes. If you want to report a status, you should use a procedure with an _inout_ formal argument.

### Break "p_ok(): by changing it to "security definer"

Try this:

```plpgsql
drop procedure if exists s.p_bad(int) cascade;
create procedure s.p_bad(v_in in int)
  security definer
  language plpgsql
as $body$
begin
  insert into s.t(v) values(v_in);
  commit;
end;
$body$;
call s.p_bad(17);
```

The _create_ statement succeeds. But the _call_ statement fails with the _2D000_ error (_invalid transaction termination_). This is a significant restriction because the canonical use case (see the section [Why use user-defined subprograms?](../#why-use-user-defined-subprograms)) calls for the _security definer_ choice.

### Break "p_ok(): by changing it to _language sql_

Try this:

```plpgsql
drop procedure if exists s.p_bad(int) cascade;
create procedure s.p_bad(v_in in int)
  security invoker
  language sql
as $body$
  insert into s.t(v) values(v_in);
  commit;
$body$;
call s.p_bad(17);
```

The _create_ statement succeeds. But the _call_ statement fails with the _0A000_ error:

```output
COMMIT is not allowed in a SQL function
```

The wording ("function" and not "procedure") survives from the pre-Version 11 era of PostgreSQL when user-defined procedures were not yet supported. (You'll see this use of "function" where "procedure" is the correct choice in other contexts too.) But the meaning, here, is clear. Of course it's also the case that a genuine user-defined _language sql_ function cannot commit--for two reasons: it's _language sql_ and it's a function.