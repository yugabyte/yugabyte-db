---
title: Helper functions for rules 2, 3, and 4 for specifying the UTC offset [YSQL]
headerTitle: Helper functions for rules 2, 3, and 4 for specifying the UTC offset
linkTitle: Helper functions
description: Code to create helper functions for substantiating rules 2, 3, and 4 for specifying the UTC offset. [YSQL]
menu:
  v2.18:
    identifier: helper-functions
    parent: name-res-rules
    weight: 50
type: docs
---

This page presents two helper functions. They are useful in substantiating [Rule 2](../rule-2/), [Rule 3](../rule-3/), and [Rule 4](../rule-4/)—explained in this page's peer pages:

## function occurrences(string in text)

This function searches for the input string in each of the relevant scopes—i.e. the columns in the two relevant catalog views:

- _pg_timezone_names.name_
- _pg_timezone_names.abbrev_
- _pg_timezone_abbrevs.abbrev_

It returns a vector of three _booleans_, one for each of the searched columns, where _true_ means "found the string in this column" and _false_ means "failed to find it".

First create the function's return type:

```plpgsql
drop type if exists occurrences_t cascade;

create type occurrences_t as(
  names_name      boolean,
  names_abbrev    boolean,
  abbrevs_abbrev  boolean);
```

Now create the function:

```plpgsql
drop function if exists occurrences(text) cascade;

create function occurrences(string in text)
  returns occurrences_t
  language plpgsql
as $body$
declare
  names_name_count constant int not null :=
    (select count(*) from pg_timezone_names   where upper(name)   = upper(string));
  names_abbrev_count constant int not null :=
    (select count(*) from pg_timezone_names   where upper(abbrev) = upper(string));
  abbrevs_abbrev_count constant int not null :=
    (select count(*) from pg_timezone_abbrevs where upper(abbrev) = upper(string));
  r constant occurrences_t not null := (
    names_name_count     > 0,
    names_abbrev_count   > 0,
    abbrevs_abbrev_count > 0)::occurrences_t;
begin
  return r;
end;
$body$;
```

## function legal_scopes_for_syntax_context(string in text)

This function tries the input string in each of the three syntax contexts where a string that's intended to specify the _UTC offset_ may be used.

- As the _set timezone_ argument

- as the _at time zone_ argument

- in specifying a _timestamptz_ value.

See [Three syntax contexts that use the specification of a _UTC offset_](../../../syntax-contexts-to-spec-offset/), the [table](../../name-res-rules/#syntax-contexts-table) in this page's parent, _"Rules for resolving a string that's intended to identify a UTC offset"_, and the examples that follow it.

The function uses the[ _occurrences()_](#function-occurrences-string-in-text) helper function to annotate the report. Create it thus:

```plpgsql
drop function if exists legal_scopes_for_syntax_context(text) cascade;

create function legal_scopes_for_syntax_context(string in text)
  returns table(x text)
  language plpgsql
as $body$
declare
  ok                       constant text          not null := '> OK';
  x1                       constant text          not null := '> invalid_parameter_value';
  x2                       constant text          not null := '> invalid_datetime_format';
  set_timezone_            constant text          not null := $$set timezone = '%s'$$;
  timezone_invocation_     constant text          not null := $$select timezone('%s', '%s')$$;
  timestamptz_literal_     constant text          not null := $$select '%s %s'::timestamptz$$;

  ts_plain                 constant timestamp     not null := '2021-06-07 12:00:00';
  ts_text                  constant text          not null := ts_plain::text;
  ts_tz                             timestamptz   not null := 'infinity'; -- any not null value will do

  set_timezone             constant text          not null := format(set_timezone_, string);
  timezone_invocation      constant text          not null := format(timezone_invocation_, string, ts_plain);
  timestamptz_literal      constant text          not null := format(timestamptz_literal_, ts_plain, string);

  set_timezone_msg         constant text          not null := rpad(set_timezone            ||';', 61);
  timezone_invocation_msg  constant text          not null := rpad(timezone_invocation     ||';', 61);
  timestamptz_literal_msg  constant text          not null := rpad(timestamptz_literal     ||';', 61);

  occurrences              constant occurrences_t not null := occurrences(string);
begin
  x := rpad(string||':', 20)                               ||
       'names_name: '    ||occurrences.names_name    ::text||' / '||
       'names_abbrev: '  ||occurrences.names_abbrev  ::text||' / '||
       'abbrevs_abbrev: '||occurrences.abbrevs_abbrev::text;                            return next;
  x := rpad('-', 90, '-');                                                              return next;

  -- "set timezone"
  begin
    execute set_timezone;
    x := set_timezone_msg||ok;                                                          return next;
  exception when invalid_parameter_value then
    x := set_timezone_msg||x1;                                                          return next;
  end;

  -- "at timezone"
  begin
    execute timezone_invocation into ts_tz;
    x := timezone_invocation_msg||ok;                                                   return next;
  exception when invalid_parameter_value then
    x := timezone_invocation_msg||x1;                                                   return next;
  end;

  begin
    execute timestamptz_literal into ts_tz;
    x := timestamptz_literal_msg||ok;                                                   return next;
  exception when invalid_datetime_format then
    x := timestamptz_literal_msg||x2;                                                   return next;
  end;
end;
$body$;
```

Test it for a selection of strings:

```plpgsql
select x from legal_scopes_for_syntax_context('WEST');
select x from legal_scopes_for_syntax_context('America/New_York');
select x from legal_scopes_for_syntax_context('XJT');
select x from legal_scopes_for_syntax_context('MST');
```

These are the results

```output
 WEST:               names_name: false / names_abbrev: true / abbrevs_abbrev: false
 ------------------------------------------------------------------------------------------
 set timezone = 'WEST';                                       > invalid_parameter_value
 select timezone('WEST', '2021-06-07 12:00:00');              > invalid_parameter_value
 select '2021-06-07 12:00:00 WEST'::timestamptz;              > invalid_datetime_format
```

and

```output
 America/New_York:   names_name: true / names_abbrev: false / abbrevs_abbrev: false
 ------------------------------------------------------------------------------------------
 set timezone = 'America/New_York';                           > OK
 select timezone('America/New_York', '2021-06-07 12:00:00');  > OK
 select '2021-06-07 12:00:00 America/New_York'::timestamptz;  > OK
```

and

```output
 XJT:                names_name: false / names_abbrev: false / abbrevs_abbrev: true
 ------------------------------------------------------------------------------------------
 set timezone = 'XJT';                                        > invalid_parameter_value
 select timezone('XJT', '2021-06-07 12:00:00');               > OK
 select '2021-06-07 12:00:00 XJT'::timestamptz;               > OK
```

and

```output
 MST:                names_name: true / names_abbrev: true / abbrevs_abbrev: true
 ------------------------------------------------------------------------------------------
 set timezone = 'MST';                                        > OK
 select timezone('MST', '2021-06-07 12:00:00');               > OK
 select '2021-06-07 12:00:00 MST'::timestamptz;               > OK
```
