---
title: Sensitivity of conversion between timestamptz and plain timestamp to the UTC offset [YSQL]
headerTitle: Sensitivity of converting between timestamptz and plain timestamp to the UTC offset
linkTitle: Timestamptz to/from timestamp conversion
description: Explains the sensitivity of conversion between timestamptz and plain timestamp to the UTC offset. [YSQL]
menu:
  v2.14:
    identifier: timestamptz-plain-timestamp-conversion
    parent: timezone-sensitive-operations
    weight: 10
type: docs
---

The semantic rules for the conversion, in each direction, rest on a common-sense convention. Each conversion uses a value for the _UTC offset_. And this value is always known: _either_ because the _at time zone_ operator specifies it (either explicitly or implicitly via a timezone name); or from the session's current _TimeZone_ setting. This is explained in the section [Four ways to specify the _UTC offset_](../../ways-to-spec-offset/).

- When a _timestamptz_ value is converted to a plain _timestamp_ value, the target is assigned to the date-and-time-of-day component of the source value, when this is expressed with respect to the inevitably known reigning _UTC offset_.
- When a plain _timestamp_ value is converted to a _timestamptz_ value, the target is assigned by normalizing to _UTC_ according to the inevitably known reigning _UTC offset_.

The rules that this section explains underly the rules for the [_text_ to _timestamptz_](../../../typecasting-between-date-time-values/#text-to-timestamptz) conversion.

## The philosophy of the demonstration's design

The demonstration uses the table function [_plain_timestamp_to_from_timestamp_tz()_](#plain-timestamp-to-from-timestamp-tz). The overall shape of this is very similar to that of these two functions:

- The table function [_interval_arithmetic_results()_](../timestamptz-interval-day-arithmetic/#interval-arithmetic-results) presented in the "sensitivity of _timestamptz-interval_ arithmetic to the current timezone" section. That section is the present section's peer under the parent section "Scenarios that are sensitive to the _UTC offset_ and possibly, additionally, to the timezone".
- The table function [_timestamptz_vs_plain_timestamp()_](../../../date-time-data-types-semantics/type-timestamp/#timestamptz-vs-plain-timestamp) presented in the "plain _timestamp_ and _timestamptz_ data types" section.

<a name="demo-goals"></a>The demonstration sets these _three_ goals:

- _Goal one:_ to use _assert_ statements to test the identity of effect of the _typecast_ operator and the _at time zone current('TimeZone')_ operator in the two directions of interest: the [plain _timestamp_ to _timestamptz_](../../../typecasting-between-date-time-values/#plain-timestamp-to-timestamptz) direction and the [_timestamptz_ to plain _timestamp_](../../../typecasting-between-date-time-values/#timestamptz-to-plain-timestamp) direction:

- _Goal two:_ to use _assert_ statements to test the _assumed rules_ for the conversions in each direction, as empirical observations suggest they might be..

- _Goal three_: to let you vizualize the rules for the conversions by outputting the _"from-to"_ value pairs for each conversion direction on a single line, doing the conversions at each of a set of representative timezones, each of which has a different _UTC offset_. (The visualization is enriched by showing the conversion outcomes first with _UTC_ as the session's current _TimeZone_ setting and then with, for each outcome, the timezone at which the conversions are done as the session's current _TimeZone_ setting.

The demonstration that follows is designed like this:

- A table function, _plain_timestamp_to_from_timestamp_tz(at_utc in boolean)_, is used to enable a convenient "running commentary" visualization. It has one formal parameter: a _boolean_ to let you specify the visualization mode.
  - One choice for _at_utc_ asks to see the plain _timestamp_ and _timestamptz_ values for each result row with the session's _TimeZone_ set to _UTC_. (Of course, only the _timestamptz_ values are affected by the setting.)
  - The other choice asks to see the values for each result row with the session's _TimeZone_ set to what was reigning when the plain _timestamp_ to _timestamptz_ and the _timestamptz_ to plain _timestamp_ conversions were done for that row.

- Two _constants_, one with data type plain _timestamp_ and one with data type _timestamptz_ are initialized so that the internal representations (as opposed to the metadata) are identical. Look:

  ```output
  ts_plain    constant timestamp   not null := make_timestamp  (yyyy, mm, dd, hh, mi, ss);
  ts_with_tz  constant timestamptz not null := make_timestamptz(yyyy, mm, dd, hh, mi, ss, 'UTC');
  ```

- Each uses the same _constant int_ values, _yyyy_, _mm_, _dd_, _hh_, _mi_, and _ss_, to define the identical _date-and-time_ part for each of the two moments. The fact that _UTC_ is used for the _timezone_ argument of the _make_timestamptz()_ invocation ensures the required identity of the internal representations of the two moments—actually, both as plain _timestamp_ values.
- The _extract(epoch from ... )_ function is used to get the numbers of seconds, as _constant double precision_ values, from the start of the epoch for the two moment values. These two numbers of seconds are actually identical. But two distinct names (_ts_plain_epoch_ and _ts_with_tz_epoch_) are used for these to help the reader see the symmetry of the two tests—one in each direction.

- A _constant_ array,  _timezones_, is initialized thus:

    ```output
    timezones constant text[] not null := array[
                                                  'Pacific/Pago_Pago',
                                                  'America/Porto_Velho',
                                                  'Atlantic/South_Georgia',
                                                  'UTC',
                                                  'Africa/Tripoli',
                                                  'Asia/Dubai',
                                                  'Pacific/Kiritimati'
                                                ];
    ```

- A _foreach_ loop is run thus:

    ```output
    foreach z in array timezones loop
    ```

- At each loop iteration:
  - The session's _TimeZone_ setting is set to the value that the iterand, _z_, specifies.

  - These assignments are made:

      ```output
      ts_with_tz_1       := ts_plain::timestamptz;
      ts_with_tz_2       := ts_plain at time zone current_setting('timezone');
      ts_with_tz_1_epoch := extract(epoch from ts_with_tz_1);

      ts_plain_1         := ts_with_tz::timestamp;
      ts_plain_2         := ts_with_tz at time zone current_setting('timezone');
      ts_plain_1_epoch   := extract(epoch from ts_plain_1);

      z_epoch            := extract(epoch from utc_offset(z));
      ```

  - These _assert_ statements are executed to show that the _::timestamp_  and _::timestamptz_ typecasts are identical to _at time zone current_setting('timezone')_:

      ```output
      assert (ts_with_tz_2 = ts_with_tz_1), 'Assert #1 failed.';
      assert (ts_plain_2   = ts_plain_1  ), 'Assert #2 failed.';
      ```

  - These _assert_ statements are executed to show that the expected rules for the conversion of the internal representations hold, in both directions between plain _timestamp_ and _timestamptz_:

  ```output
  assert ( ts_with_tz_1_epoch = (ts_plain_epoch   - z_epoch) ), 'Assert #3 failed.';
  assert ( ts_plain_1_epoch   = (ts_with_tz_epoch + z_epoch) ), 'Assert #4 failed.';
  ```

  - According to the choice for _at_utc:_ _either_ the timezone is set to _UTC;_ _or_ it is simply left at what the loop iterand, _z_, set it to. Then these values are formatted as a _text_ line using the _to_char()_ built-in function: _ts_plain_, _ts_with_tz_1_, _ts_with_tz_, and _ts_plain_1_. The row is labeled with the value of the loop iterand, _z_.

- Finally, after the loop completes and before exiting, the session's _TimeZone_ setting is restored to the value that it had on entry to the function. (It's always good practice to do this for any settings that your programs need, temporarily, to change.)

## The demonstration

First, create a trivial function to return the _pg_timezone_names.utc_offset_ value for a specified value of _pg_timezonenames.name_:

```plpgsql
drop function if exists utc_offset(text) cascade;
create function utc_offset(tz_name in text)
  returns interval
  language plpgsql
as $body$
declare
  i constant interval not null := (
                                    select a.utc_offset from
                                    pg_timezone_names a
                                    where a.name = tz_name
                                  );
begin
  return i;
end;
$body$;
```

Now create a wrapper to return the _utc_offset()_'s returned _interval_ value as _text_ in exactly the format that the literal for a _timestamptz_ value uses.

```plpgsql
drop function if exists utc_offset_display(text) cascade;
create function utc_offset_display(tz_name in text)
  returns text
  language plpgsql
as $body$
declare
  i constant interval not null := utc_offset(tz_name);
  hh constant int     not null := extract(hours   from i);
  mm constant int     not null := extract(minutes from i);
  t  constant text    not null := to_char(hh, 's00')||
                                    case
                                      when mm <> 0 then ':'||ltrim(to_char(mm, '00'))
                                      else               ''
                                    end;
begin
  return t;
end;
$body$;
```

Now create two formatting functions:
- one to format the results for maximum readability
- and another to format the column headers.

It's dull code, and you needn't read it. You can understand what it does by its effect. It's nice to package away the dull code so that it doesn't clutter the interesting logic of the main _plain_timestamp_to_from_timestamp_tz()_ table function.

```plpgsql
create function report_line(
  z             in text,
  ts_plain      in timestamp,
  ts_with_tz_1  in timestamptz,
  ts_with_tz    in timestamptz,
  ts_plain_1    in timestamp)
  returns text
  language plpgsql
as $body$
declare
  t constant text not null :=
      '['||rpad(z, 23)||lpad(utc_offset_display(z), 3)||']'     ||'   '||
           rpad(to_char(ts_plain,     'Dy hh24:mi'        ),  9)||'   '||
           rpad(to_char(ts_with_tz_1, 'Dy hh24:mi TZH:TZM'), 16)||'      '||
           rpad(to_char(ts_with_tz,   'Dy hh24:mi TZH:TZM'), 16)||'   '||
           rpad(to_char(ts_plain_1,   'Dy hh24:mi'        ),  9);
begin
  return t;
end;
$body$;

drop function if exists headers() cascade;
create function headers()
  returns text[]
  language plpgsql
as $body$
declare
  t text[] := array['x', 'x', 'x'];
begin
  t[1] :=
         rpad(' ',           28)           ||'   '||
         rpad('From',         9)           ||'   '||
         rpad('To',  16)                   ||'      '||
         rpad('From',  16)                 ||'   '||
              'To';

  t[2] :=
    '['||rpad('Timezone',    20)||'Offset]'||'   '||
         rpad('ts_plain',     9)           ||'   '||
         rpad('ts_with_tz',  16)           ||'      '||
         rpad('ts_with_tz',  16)           ||'   '||
              'ts_plain';

  t[3] :=
         rpad('-',      28, '-')           ||'   '||
         rpad('-',       9, '-')           ||'   '||
         rpad('-',      16, '-')           ||'      '||
         rpad('-',      16, '-')           ||'   '||
         rpad('-', 9, '-');
  return t;
end;
$body$;
```

<a name="plain-timestamp-to-from-timestamp-tz"></a>Now create and execute the _plain_timestamp_to_from_timestamp_tz()_ table function thus.

```plpgsql
drop function if exists plain_timestamp_to_from_timestamp_tz(boolean) cascade;

create function plain_timestamp_to_from_timestamp_tz(at_utc in boolean)
  returns table(t text)
  language plpgsql
as $body$
declare
  set_timezone      constant text             not null := $$set timezone = '%s'$$;
  tz_on_entry       constant text             not null := current_setting('timezone');

  yyyy              constant int              not null := 2000;
  mm                constant int              not null := 1;
  dd                constant int              not null := 1;
  hh                constant int              not null := 10;
  mi                constant int              not null := 15;
  ss                constant int              not null := 0;
  ts_plain          constant timestamp        not null := make_timestamp  (yyyy, mm, dd, hh, mi, ss);
  ts_with_tz        constant timestamptz      not null := make_timestamptz(yyyy, mm, dd, hh, mi, ss, 'UTC');
  ts_plain_epoch    constant double precision not null := extract(epoch from ts_plain);
  ts_with_tz_epoch  constant double precision not null := extract(epoch from ts_with_tz);
begin
  t := '---------------------------------------------------------------------------------------------';     return next;
  if at_utc then
    t := 'Displaying all results using UTC.';                                                               return next;
  else
    t := 'Displaying each set of results using the timezone in which they were computed.';                  return next;
  end if;
  t := '---------------------------------------------------------------------------------------------';     return next;
  t := '';                                                                                                  return next;

  declare
    ts constant text[] not null := headers();
  begin
    foreach t in array ts loop
                                                                                                            return next;
    end loop;
  end;

  declare
    z                  text   not null := '';
    timezones constant text[] not null := array[
                                                 'Pacific/Pago_Pago',
                                                 'America/Porto_Velho',
                                                 'Atlantic/South_Georgia',
                                                 'UTC',
                                                 'Africa/Tripoli',
                                                 'Asia/Dubai',
                                                 'Pacific/Kiritimati'
                                               ];
  begin
    foreach z in array timezones loop
      execute format(set_timezone, z);

      declare
        ts_with_tz_1        constant timestamptz      not null := ts_plain::timestamptz;
        ts_with_tz_2        constant timestamptz      not null := ts_plain at time zone current_setting('timezone');
        ts_with_tz_1_epoch  constant double precision not null := extract(epoch from ts_with_tz_1);

        ts_plain_1          constant timestamp        not null := ts_with_tz::timestamp;
        ts_plain_2          constant timestamp        not null := ts_with_tz at time zone current_setting('timezone');
        ts_plain_1_epoch    constant double precision not null := extract(epoch from ts_plain_1);

        z_epoch             constant double precision not null := extract(epoch from utc_offset(z));
      begin
        -- Show that "::timestamp" is identical to "at time zone current_setting('timezone')".
        assert (ts_with_tz_2 = ts_with_tz_1), 'Assert #1 failed.';
        assert (ts_plain_2   = ts_plain_1  ), 'Assert #2 failed.';

        -- Show that the expected rules for the conversion of the internal representations,
        -- in both plain timestamp to/from timestamptz directions, hold.
        assert ( ts_with_tz_1_epoch = (ts_plain_epoch   - z_epoch) ), 'Assert #3 failed.';
        assert ( ts_plain_1_epoch   = (ts_with_tz_epoch + z_epoch) ), 'Assert #4 failed.';

        /* Display the internally represented values:
             EITHER: using 'UTC' to show what they "really" are
             OR:     using the timezone in which they were computed to show
                     the intended usability benefit for the local observer. */
        if at_utc then
          execute format(set_timezone, 'UTC');
          -- Else, leave the timezone set to "z".
        end if;
        t := report_line(z, ts_plain, ts_with_tz_1, ts_with_tz, ts_plain_1);                                return next;
      end;
    end loop;
  end;

  execute format(set_timezone, tz_on_entry);
end;
$body$;

select t from plain_timestamp_to_from_timestamp_tz(true);
```

This is the result:

```output
---------------------------------------------------------------------------------------------
Displaying all results using UTC.
---------------------------------------------------------------------------------------------

                              From        To                    From               To
[Timezone            Offset]   ts_plain    ts_with_tz            ts_with_tz         ts_plain
----------------------------   ---------   ----------------      ----------------   ---------
[Pacific/Pago_Pago      -11]   Sat 10:15   Sat 21:15 +00:00      Sat 10:15 +00:00   Fri 23:15
[America/Porto_Velho    -04]   Sat 10:15   Sat 14:15 +00:00      Sat 10:15 +00:00   Sat 06:15
[Atlantic/South_Georgia -02]   Sat 10:15   Sat 12:15 +00:00      Sat 10:15 +00:00   Sat 08:15
[UTC                    +00]   Sat 10:15   Sat 10:15 +00:00      Sat 10:15 +00:00   Sat 10:15
[Africa/Tripoli         +02]   Sat 10:15   Sat 08:15 +00:00      Sat 10:15 +00:00   Sat 12:15
[Asia/Dubai             +04]   Sat 10:15   Sat 06:15 +00:00      Sat 10:15 +00:00   Sat 14:15
[Pacific/Kiritimati     +14]   Sat 10:15   Fri 20:15 +00:00      Sat 10:15 +00:00   Sun 00:15
```

The execution finishes without error, confirming that the assertions hold.

## Interpretation and statement of the rules

{{< tip title="Underlying axiom." >}}
The interpretation of the demonstration's outcome depends on this fact, stated and empirically confirmed in the [plain _timestamp_ and _timestamptz_ data types](../../../date-time-data-types-semantics/type-timestamp/) section:

> Both a plain _timestamp_ datum and a _timestamptz_ datum have the identical internal representation as the number of seconds from a reference moment. The _extract(epoch from t)_ function, where _t_ is either a plain _timestamp_ value or a _timestamptz_ value, returns this number. Moreover, the result is independent of the session's current _TimeZone_ setting.
{{< /tip >}}

The demonstration meets the goals set out in the "The philosophy of the demonstration's design" section:

- _[Goal one](#demo-goals)_ is met because there are no _assert_ violations: these two properties of the mutual conversion shown in the paragraph that defines this goal are seen to hold.</br>

  ```output
  IF:
    ts_with_tz_1 ◄— ts_plain::timestamptz
  AND:
    ts_with_tz_2 ◄— ts_plain at time zone current_setting('timezone')
  THEN:
    ts_with_tz_2 == ts_with_tz_1
  ```

  </br>_and:_

  ```output
  IF:
    ts_plain_1 ◄— ts_with_tz::timestamp
  AND:
    ts_plain_2 ◄— ts_with_tz at time zone current_setting('timezone')
  THEN:
    ts_plain_2 == ts_plain_1
  ```
  </br>

- _[Goal two](#demo-goals)_ is met because there are no _assert_ violations: these rules for the mutual conversions are seen to hold.

  ```output
  ts-with-tz-internal-seconds ◄— ts-plain-internal-seconds - specified-utc-offset-in-seconds
  ```

  </br>_and:_

  ```output
  ts-plain-internal-seconds ◄— ts-with-tz-internal-seconds + specified-utc-offset-in-seconds
  ```
  </br>

- _[Goal three](#demo-goals)_ is met by inspecting the output immediately above and by Invoking the table function again to show each result row using the timezone in which it was computed.

  ```plpgsql
  select t from plain_timestamp_to_from_timestamp_tz(false);
  ```

  </br>This is the new result:

  ```output
  ---------------------------------------------------------------------------------------------
  Displaying each set of results using the timezone in which they were computed.
  ---------------------------------------------------------------------------------------------

                                From        To                    From               To
  [Timezone            Offset]   ts_plain    ts_with_tz            ts_with_tz         ts_plain
  ----------------------------   ---------   ----------------      ----------------   ---------
  [Pacific/Pago_Pago      -11]   Sat 10:15   Sat 10:15 -11:00      Fri 23:15 -11:00   Fri 23:15
  [America/Porto_Velho    -04]   Sat 10:15   Sat 10:15 -04:00      Sat 06:15 -04:00   Sat 06:15
  [Atlantic/South_Georgia -02]   Sat 10:15   Sat 10:15 -02:00      Sat 08:15 -02:00   Sat 08:15
  [UTC                    +00]   Sat 10:15   Sat 10:15 +00:00      Sat 10:15 +00:00   Sat 10:15
  [Africa/Tripoli         +02]   Sat 10:15   Sat 10:15 +02:00      Sat 12:15 +02:00   Sat 12:15
  [Asia/Dubai             +04]   Sat 10:15   Sat 10:15 +04:00      Sat 14:15 +04:00   Sat 14:15
  [Pacific/Kiritimati     +14]   Sat 10:15   Sat 10:15 +14:00      Sun 00:15 +14:00   Sun 00:15
  ```

### The results at _"Displaying all results using UTC"_

These show what is really happening at the level of the internal representation—albeit that you have to deal with a degree of circularity of logic to accept this claim. It's equivalent to looking at the numbers of seconds from _12:00_ on _1-Jan-1070_. (You could write your own formatter, using the _trunc()_ and _mod()_ built-in functions, to produce the same display.) Of course, only the display of _timestamptz_ values is sensitive to the current value of the session's _TimeZone_ setting.

The _"From ts_plain"_ column shows the same value in each row—as is to be expected. The _"To ts_with_tz"_ column shows a different value in each row, supporting this informal statement of the rule:

- The _timestamptz_ value is produced by assuming that the to-be-converted plain _timestamp_ value represents local wall-clock time in the timezone in which the conversion is done.

The _"From ts_with_tz"_ column shows the same value in each row—again as is to be expected. The _"To ts_plain"_ column shows a different value in each row, supporting this informal statement of the rule:

- The plain _timestamp_ value is produced by assuming that the to-be-converted _timestamptz_'s displayed date and time of day, as these would be shown in the timezone at which the conversion is done, jointly represent the target plain _timestamp_ value.

### The results at _"Displaying each set of results using the timezone in which they were computed"_

The displayed _text_ values, of course, represent the same underlying internally represented values as do the displayed values in the results at _"Displaying all results using UTC"_. And, again of course, the _text_ values in the columns for the plain _timestamp_ values are pairwise identical, row by row, in the two sets of results. But the _text_ values in the columns for the _timestamptz_ values are pairwise different, row by row, in the two sets of results—just as they should be to bring the intended usability value of the data type. The rules are (arguably) easier to understand in the second presentation.

The _"To ts_with_tz"_ column shows the same date-and-time value in each row, but the displayed _UTC offset_ at which this is to be understood is different in each row (and equal to the offset that the reigning timezone specifies). The _"To ts_with_tz"_ column shows a different value in each row, supporting this informal statement of the rule:

- The to-be-converted plain _timestamp_ value is simply decorated with the offset of the timezone in which the conversion is done.

The _"To ts_plain"_ column shows a different value in each row. But this value is identical to the date-and-time value of the to-be-converted _timestamptz_ value, supporting this informal statement of the rule:

- The offset of the displayed to-be-converted _timestamptz_ value is simply ignored and the target plain _timestamp_ value is set to the to-be-converted value's _displayed_ date-and-time component.

You should aim to be comfortable with these three different ways to state the rules:

- _First_ in terms of the internally represented seconds and the magnitude of the reigning _UTC offset_ in seconds.
- _Second_ in terms of the _text_ display of the results using _UTC_.
- _Third_ in terms of the _text_ display of the results using the timezones at which the conversions are done.

Notice that the first way to state the rules is by far the most terse and precise—and therefore the most reliable. The other two ways are subject to the limitations of composing, and interpreting, tortuous natural language prose.
