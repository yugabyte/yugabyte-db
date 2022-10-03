---
title: Rule 1 (for string intended to specify the UTC offset) [YSQL]
headerTitle: Rule 1
linkTitle: 1 case-insensitive resolution
description: Substantiates the rule that a string that's intended to identify a UTC offset is resolved case-insensitively. [YSQL]
menu:
  preview:
    identifier: rule-1
    parent: name-res-rules
    weight: 10
type: docs
---

{{< tip title="" >}}
A string that's intended to identify a _UTC offset_ is resolved case-insensitively. (This rule excludes, of course, selecting explicitly from one of the _pg_timezone_names_ or _pg_timezone_abbrevs catalog_ views).
{{< /tip >}}

The anonymous PL/pgSQL block below looks up the name _America/Costa_Rica_, spelled exactly with that mix of case, in _pg_timezone_names_ and returns its abbreviation. Because an ordinary query with a case-sensitive predicate is used, this confirms the canonical status of the spelling.

The canonically spelled name _America/Costa_Rica_ is also used to return _utc_offset_ from _pg_timezone_names_. And the abbreviation is used to return _utc_offset_ from _pg_timezone_abbrevs_. It isn't always the case that a timezone abbreviation from _pg_timezone_names_ is found in _pg_timezone_abbrevs_. But the fact that the query used here returns a value without error shows that, in this case, a match is found. An _assert_ statement tests that the two possibly different offset values agree. (This, too, isn't always the case.)

Another _assert_ statement confirms that the abbreviation is not found as a name in _pg_timezone_names_. (This sometimes is the case.)

Four different variants of the timezone name, each spelling it with a different mix of case, are defined.

An _assert_ statement (_"Assert #1"_) confirms that invoking _current_setting('TimeZone')_ returns the canonical spelling when the timezone is set using one of the case-variants of the timezone name.

Another three case-variants are used, each in a different syntax context, to produce a _timestamptz_ value. And _assert_ statements (_"Assert #2"_ and _"Assert #3"_) are used to confirm that the three differently-produced _timestamptz_ values are all the same.

Finally, the result of the _lower()_ built-in function on the abbreviation is used to construct a _timestamptz_ literal; and the _upper()_ result is used as the _at time zone_ argument. And _assert_ statements (_"Assert #4"_ and _"Assert #5"_) are used to confirm that these two _timestamptz_ values are the same as the first three.

Try the block like this:

```plpgsql
do $body$
declare
  -- The spelling of "nm" is the canonical one, as is recorded in "pg_timezone_names"
  -- and as is returned by "current_setting('TimeZone')".
  nm constant text     not null := 'America/Costa_Rica';

  -- Case variants. Each is used once in a different syntax setting.
  n1 constant text     not null := 'AMERICA/COSTA_RICA';
  n2 constant text     not null := 'america/costa_rica';
  n3 constant text     not null := 'AMERICA/costa_rica';
  n4 constant text     not null := 'america/COSTA_RICA';

  ab constant text     not null := (select abbrev     from pg_timezone_names   where name   = nm);
  on constant interval not null := (select utc_offset from pg_timezone_names   where name   = nm);
  oa constant interval not null := (select utc_offset from pg_timezone_abbrevs where abbrev = ab);
  c  constant int      not null := (select count(*)   from pg_timezone_names   where name   = ab);

  ------------------------------------------------------------------------------------------------------------

  set_timezone  constant text not null := $$set timezone = '%s'$$;
  tz_on_entry   constant text not null := current_setting('TimeZone');
begin
  assert oa = oa,    'name/abbrev UTC offsets disagree';
  assert c  = 0,     'Abbrev found in pg_timezone_names.name';

  declare
    lower_ab  constant text not null := lower(ab);
    upper_ab  constant text not null := upper(ab);
    dt_text   constant text not null := '2021-05-15 12:00:00';
    t0        constant timestamptz not null := make_timestamptz(2021, 5, 15, 12, 0, 0, n1);
  begin
    execute format(set_timezone, n2);
    assert
      current_setting('timezone') = nm,
      'Assert #1 failed';

    assert
      (dt_text||' '||n3)::timestamptz = t0,
      'Assert #2 failed';

    assert
      (dt_text::timestamp at time zone n4) = t0,
      'Assert #3 failed';

    assert
      (dt_text||' '||lower_ab)::timestamptz = t0,
      'Assert #4 failed';

    assert
      (dt_text::timestamp at time zone upper_ab) = t0,
      'Assert #5 failed';
  end;

  execute format(set_timezone, tz_on_entry);
end;
$body$;
```

The block finishes silently.

**This outcome supports the formulation of the rule that this page addresses.**
