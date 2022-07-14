---
title: Rule 4 (for string intended to specify the UTC offset) [YSQL]
headerTitle: Rule 4
linkTitle: 4 ~abbrevs.abbrev before ~names.name
description: Substantiates the rule that a string that's intended to identify a UTC offset is resolved first in pg_timezone_abbrevs.abbrev and, only if this fails, then in pg_timezone_names.name. [YSQL]
menu:
  stable:
    identifier: rule-4
    parent: name-res-rules
    weight: 40
type: docs
---

{{< tip title="" >}}
A string that's intended to identify _a UTC_ offset is resolved first in _pg_timezone_abbrevs.abbrev_ and, only if this fails, then in _pg_timezone_names.name_.

This applies only in those syntax contexts where _pg_timezone_abbrevs.abbrev_ is a candidate for the resolution—so not for _set timezone_, which looks only in _pg_timezone_names.name_.
{{< /tip >}}</br>

{{< tip title="Download and install the date-time utilities code." >}}
The code on this page depends on the code presented in the [_extended_timezone_names_ view](../../../extended-timezone-names/) section. This is included in the larger [code kit](../../../../download-date-time-utilities/) that includes all of the reusable code that the overall _[date-time](../../../../../type_datetime/)_ section describes and uses.
{{< /tip >}}

The page for [Rule 3](../rule-3) tested with a string that's found uniquely in _pg_timezone_abbrevs.abbrev_. It established that for the second two syntax contexts (the _at time zone_ operator and the _text_ literal for a _timestamptz_ value), the string _is_ looked up in this column; and that for the first syntax context (the _set timezone_ statement) this column is _not_ searched.

## Test with a string that's found uniquely in 'pg_timezone_names.name'

You can discover, with _ad hoc_ queries. that the string _Europe/Amsterdam_ occurs only in _pg_timezone_names.name_. Use the function [_occurrences()_](../helper-functions/#function-occurrences-string-in-text) to confirm it thus

```plpgsql
with c as (select occurrences('Europe/Amsterdam') as r)
select
  (c.r).names_name     ::text as "~names.name",
  (c.r).names_abbrev   ::text as "~names.abbrev",
  (c.r).abbrevs_abbrev ::text as "~abbrevs.abbrev"
from c;
```

This is the result:

```output
 ~names.name | ~names.abbrev | ~abbrevs.abbrev
-------------+---------------+-----------------
 true        | false         | false
```

This means that the string _Europe/Amsterdam_ can be used as a probe, using the function [_legal_scopes_for_syntax_context()_](../helper-functions/#function-legal-scopes-for-syntax-context-string-in-text)_:

```plpgsql
select x from legal_scopes_for_syntax_context('Europe/Amsterdam');
```

This is the result:

```output
 Europe/Amsterdam:   names_name: true / names_abbrev: false / abbrevs_abbrev: false
 ------------------------------------------------------------------------------------------
 set timezone = 'Europe/Amsterdam';                           > OK
 select timezone('Europe/Amsterdam', '2021-06-07 12:00:00');  > OK
 select '2021-06-07 12:00:00 Europe/Amsterdam'::timestamptz;  > OK
```

So _pg_timezone_names.name_ is searched in each of the three syntax contexts.

## Test with a string that's found both in 'pg_timezone_names.name' and in 'pg_timezone_abbrevs.abbrev'

The outcomes of the test that substantiated [Rule-3](../rule-3) and of the test [above](#test-with-a-string-that-s-found-uniquely-in-pg-timezone-names-names) raise the question of priority: what if the string that's intended to specify the _UTC offset_ occurs in _both_ columns?

You can discover, with _ad hoc_ queries. that the string _MET_ occurs both in _pg_timezone_names.name_ and in _pg_timezone_abbrevs.abbrev_. Use the function [_occurrences()_](../helper-functions/#function-occurrences-string-in-text) to confirm it thus

```plpgsql
with c as (select occurrences('MET') as r)
select
  (c.r).names_name     ::text as "~names.name",
  (c.r).names_abbrev   ::text as "~names.abbrev",
  (c.r).abbrevs_abbrev ::text as "~abbrevs.abbrev"
from c;
```

This is the result:

```output
 ~names.name | ~names.abbrev | ~abbrevs.abbrev
-------------+---------------+-----------------
 true        | false         | true
```

This means that the string _MET_ can be used as a probe, using the function [_legal_scopes_for_syntax_context()_](../helper-functions/#function-legal-scopes-for-syntax-context-string-in-text)_:

```plpgsql
select x from legal_scopes_for_syntax_context('MET');
```

Predictably, this is the result:

```output
 MET:                names_name: true / names_abbrev: false / abbrevs_abbrev: true
 ------------------------------------------------------------------------------------------
 set timezone = 'MET';                                        > OK
 select timezone('MET', '2021-06-07 12:00:00');               > OK
 select '2021-06-07 12:00:00 MET'::timestamptz;               > OK
```

## Who wins?

The [PostgresSQL documentation](https://www.postgresql.org/docs/11/) does not provide the answer. But the question can be answered empirically if _MET_ (or another such string that occurs in both columns) maps to different _UTC_offset_ values in the two different columns. Try this:

```plpgsql
with
  met_names_offsets(string, names_offset, is_dst) as (
    select name, utc_offset, is_dst
    from pg_timezone_names
    where name = 'MET'),

  met_abbrevs_offset(string, abbrevs_offset) as (
    select abbrev, utc_offset
    from pg_timezone_abbrevs
    where abbrev = 'MET'),

possibly_disagreeing_offsets(string, names_offset, is_dst, abbrevs_offset) as (
  select string, n.names_offset, n.is_dst, a.abbrevs_offset
  from
    met_names_offsets as n
    inner join
    met_abbrevs_offset as a
    using(string))

select string, names_offset, is_dst::text, abbrevs_offset
from possibly_disagreeing_offsets;
```

This is the result:

```output
 string | names_offset | is_dst | abbrevs_offset
--------+--------------+--------+----------------
 MET    | 02:00:00     | true   | 01:00:00
```

Of course, there is just one row because both  _pg_timezone_names.name_ and _pg_timezone_abbrevs.abbrev_ have unique values. You can see that the query happens to have been executed during the Day Light Savings Time period for the timezone _MET_. This is fortunate for the usefulness of the test that follows. Look up _MET_ in the [_extended_timezone_names_](../../../extended-timezone-names/) view.

```plpgsql
select name, std_abbrev, dst_abbrev, std_offset, dst_offset
from extended_timezone_names
where name = 'MET';
```

This is the result:

```output
 name | std_abbrev | dst_abbrev | std_offset | dst_offset
------+------------+------------+------------+------------
 MET  | MET        | MEST       | 01:00:00   | 02:00:00
```

So the test that follows would not be useful during _MET's_ winter.

## Can the test be carried out during the winter?

It turns out that no string exists that has the properties needed to do the test in the winter:

- The string occurs both in _pg_timezone_names.name_ and _pg_timezone_abbrevs.abbrev_.
- _pg_timezone_names.utc_offset_ and _pg_timezone_abbrevs.utc_offset_, for that string, differ during the winter.

You can see that with this query:

```plpgsql
with
  ambiguous_strings(string) as (
    select name from pg_timezone_names
    intersect
    select abbrev from pg_timezone_abbrevs),

  possibly_disagreeing_offsets(
    string,
    std_abbrev,
    dst_abbrev,
    std_offset,
    dst_offset,
    abbrevs_offset)
  as (
    select
      e.name,
      e.std_abbrev,
      e.dst_abbrev,
      e.std_offset,
      e.dst_offset,
      (
        select utc_offset
        from pg_timezone_abbrevs a1
        where a1.abbrev = e.name
      )
    from extended_timezone_names e
    where e.name in (
      select a2.string from ambiguous_strings a2))

select
  string,
  std_abbrev,
  dst_abbrev,
  lpad(std_offset     ::text, 9) as "std offset from ~names",
  lpad(dst_offset     ::text, 9) as "dst offset from ~names",
  lpad(abbrevs_offset ::text, 9) as "offset from ~abbrevs"
from possibly_disagreeing_offsets
order by string;
```

This is the result:

```outout
 string | std_abbrev | dst_abbrev | std offset from ~names | dst offset from ~names | offset from ~abbrevs
--------+------------+------------+------------------------+------------------------+----------------------
 CET    | CET        | CEST       |  01:00:00              |  02:00:00              |  01:00:00
 EET    | EET        | EEST       |  02:00:00              |  03:00:00              |  02:00:00

 EST    | EST        | EST        | -05:00:00              | -05:00:00              | -05:00:00
 GMT    | GMT        | GMT        |  00:00:00              |  00:00:00              |  00:00:00
 HST    | HST        | HST        | -10:00:00              | -10:00:00              | -10:00:00

 MET    | MET        | MEST       |  01:00:00              |  02:00:00              |  01:00:00

 MST    | MST        | MST        | -07:00:00              | -07:00:00              | -07:00:00
 UCT    | UCT        | UCT        |  00:00:00              |  00:00:00              |  00:00:00
 UTC    | UTC        | UTC        |  00:00:00              |  00:00:00              |  00:00:00

 WET    | WET        | WEST       |  00:00:00              |  01:00:00              |  00:00:00
```

The blank lines were added by hand to highlight the rows where the value of _"offset from ~abbrevs"_ differs from one of _"std offset from ~names"_ or _"dst offset from ~names"_. Notice that when it does differ, it always differs from the summer value. This means that the test cannot be carried out in the winter.

The names with the summer difference are _CET_, _EET_, _MET_, and _WET_.

Try the following exhaustive demonstration of the priority rule. (Of course, the demonstration will work only in the summer!) The test design rests on the rule that was established for the case that the string that specifies the _UTC offset_ specifies the same value in both the _::timestamptz_ and the _at time zone_ syntax contexts  [here](../../../timezone-sensitive-operations/timestamptz-plain-timestamp-conversion/#goal-one-met) in the section _"Sensitivity of converting between timestamptz and plain timestamp to the UTC offset"_.

But, here, there is a critical difference in how the rule is formulated. It's formulated here to cover the conventional _a priori_ assumption that's made at the application design stage when choosing between the two alternative ways to convert a plain _timestamp_ value to a _timestamptz_ value, thus:

```output
  IF:
    ts_with_tz_1 ◄— ts_plain::timestamptz # Following « set timezone = 'the_string' »
  AND:
    ts_with_tz_2 ◄— ts_plain at time zone 'the_string'
  THEN:
    ts_with_tz_2 == ts_with_tz_1
```

Execute the test for a set of two kinds of string, as the comments in the initialization code of the _strings text[]_ array explain. Notice that the _if_ test means that the value of _string_ is output only when the _timestamptz_ values produced by the two different syntaxes disagree. And for each such output, it shows the difference (as an _interval_ value, of course) between the two disagreeing _timestamptz_ values

```plpgsql
drop function if exists priority_rule_demo() cascade;

create function priority_rule_demo()
  returns table(z text)
  language plpgsql
as $body$
declare
  set_timezone  constant text      not null := $$set timezone = '%s'$$;
  tz_on_entry   constant text      not null := current_setting('timezone');
  t0            constant timestamp not null := '2021-06-20 12:00:00'::timestamp;
  the_string             text      not null := '';
  strings       constant text[]    not null := array
                                                    [
                  /* These offset strings occur */    'Pacific/Pago_Pago',
                  /* uniquely in "~names.name"  */    'America/Porto_Velho',
                                                      'Atlantic/South_Georgia',
                                                      'Africa/Tripoli',
                                                      'Asia/Dubai',
                                                      'Pacific/Kiritimati',

                  /* These offset strings occur */    'CET',
                  /* both in "~names.name"      */    'EET',
                  /* and in "~abbrevs.abbrev"   */    'MET',
                                                      'WET'
                                                      ];
begin
  z := rpad('Timezone', 25)||lpad('t0::timestamptz - t0 at time zone "the_string"', 49);   return next;
  z := rpad('-', 25, '-')  ||lpad('----------------------------------------------', 49);   return next;
  foreach the_string in array strings loop
  execute format(set_timezone, the_string);
    declare
      t1    constant timestamptz      not null := t0::timestamptz;
      e1    constant double precision not null := extract(epoch from t1);

      t2    constant timestamptz      not null := t0 at time zone the_string;
      e2    constant double precision not null := extract(epoch from t2);

      diff  constant interval         not null := make_interval(secs=>(e2 - e1));
    begin
      if e1 <> e2 then
        z :=
          rpad(the_string, 25)||
          lpad(to_char(diff, 'hh24:mi')::text, 49);                                        return next;
      end if;
    end;
  end loop;

execute format(set_timezone, tz_on_entry);
end;
$body$;

select z from priority_rule_demo();
```

This is the result:

```output
 Timezone                    t0::timestamptz - t0 at time zone "the_string"
 -------------------------   ----------------------------------------------
 CET                                                                  01:00
 EET                                                                  01:00
 MET                                                                  01:00
 WET                                                                  01:00
```

Notice that the difference, when it's non-zero, is always equal to the difference between the _UTC offset_ values read from the _"~names.name"_ column and the _"~abbrevs.abbrev"_ column— _one hour_ in each case.

**This outcome supports the formulation of the rule that this page addresses.**

The results also highlight an insidious risk. Suppose that a developer doesn't know the priority rule and assumes (erroneously, but arguably reasonably) that a timezone name never occurs in _pg_timezone_abbrevs.abbrev_ (or, maybe, that if it did then _pg_timezone_names.name_ would win). And assume that she carries out acceptance tests of her application code, using any of the four timezone names where the _~names_ offset and the _~abbrevs_ offset differ only in the summer—and that she does this testing in the winter. All will seem to be good. And then the summer will bring silent wrong results!

Yugabyte recommends that you program your application code defensively so that you explicitly avoid this risk by ensuring that names that are used to specify the _UTC offset_ occur _only_ in _pg_timezone_names.name_. The section [Recommended practice for specifying the _UTC offset_](../../../recommendation/) explains how to do this.
