---
title: Implementations that model the overlaps operator [YSQL]
headerTitle: Implementations that model the overlaps operator
linkTitle: Implementations that model the overlaps operator
description: Implementations that model the overlaps operator. [YSQL]
menu:
  stable:
    identifier: overlaps
    parent: miscellaneous
    weight: 30
type: docs
---

## The function modeled_overlaps(timestamp, timestamp, timestamp, timestamp) returns boolean

This function models the _overlaps_ semantics using two distinct approaches:

- Using the `&&` operator with two _range_ values.
- From first principles using ordinary if-the-else logic.

It considers just this invocation syntax:

```output
overlaps_result ◄— (left-duration-bound-1, left-duration-bound-2) overlaps (right-duration-bound-1, right-duration-bound-2)
```

The other overloads:

- that define one duration using a pair of moments and one using a moment and a duration size
- that define both durations using a moment and a duration size

can be trivially expressed as wrappers around the overload that defines both durations using a pair of moments.

The model addresses just the overload that specifies each duration by a pair of plain _timestamp_ bounds. The corresponding overloads for plain _time_ and _timestamptz_ can be trivially derived from this.

This page's parent page, in the section [_overlaps_ semantics in prose](../#overlaps-semantics-in-prose), explained that the _overlaps_ operator result is insensitive, for each of the two durations on which it acts, to the ordering of the moment arguments that define the duration's bounds—and that it is also insensitive to which duration is given as the left duration and which is given as the right duration. However, the implementation needs to distinguish between the start and the finish moments and needs to know how the two durations are mutually ordered. In other words, it models this invocation syntax:

```output
overlaps_result ◄— (earlier-duration-start-moment, earlier-duration-finish-moment) overlaps (later-duration-start-moment, later-duration-finish-moment)
```

The implementation therefore starts by deriving these values:

```output
earlier_start, earlier_finish, later_start, later_finish
```

from these input values:

```output
left_1, left_2, right_1, right_2
```

Create the function _modeled_overlaps()_ thus:

```plpgsql
drop function if exists modeled_overlaps(timestamp, timestamp, timestamp, timestamp) cascade;

create function modeled_overlaps(left_1 in timestamp, left_2 in timestamp, right_1 in timestamp, right_2 in timestamp)
  returns boolean
  language plpgsql
as $body$
declare
  left_start       constant timestamp not null := least   (left_1,  left_2);
  left_finish      constant timestamp not null := greatest(left_1,  left_2);
  right_start      constant timestamp not null := least   (right_1, right_2);
  right_finish     constant timestamp not null := greatest(right_1, right_2);

  left_is_earlier  constant boolean   not null :=
    case
      when left_start < right_start then true
      when left_start > right_start then false
      else
        -- Here when left_start and right_start coincide
        case
          when left_finish < right_finish then true
          else                           false
        end
    end;

  earlier_start    timestamp not null := '-infinity'; -- convenient initial value;
  earlier_finish   timestamp not null := earlier_start;
  later_start      timestamp not null := earlier_start;
  later_finish     timestamp not null := earlier_start;
begin
  if left_is_earlier then
    earlier_start  := left_start;
    earlier_finish := left_finish;
    later_start    := right_start;
    later_finish   := right_finish;
  else
    earlier_start  := right_start;
    earlier_finish := right_finish;
    later_start    := left_start;
    later_finish   := left_finish;
  end if;
  declare
    -- Modeled using the && operator with two range values.
    -- Accommodate the fact that if the bounds of a '[)' range, r, coincide,
    -- then isempty(r) evaluates to TRUE by using a '[]' range instead.
    r1 constant tsrange not null :=
      case
        when earlier_start = earlier_finish then tsrange(earlier_start, earlier_finish, '[]')
        else                                     tsrange(earlier_start, earlier_finish, '[)')
      end;

    r2 constant tsrange not null :=
      case
        when later_start = later_finish then tsrange(later_start, later_finish, '[]')
        else                                 tsrange(later_start, later_finish, '[)')
      end;

    modeled_using_ranges boolean not null := r1 && r2;

    -- Modeled from first principles using ordinary if-the-else logic.
    modeled_from_first_principles boolean not null :=
      case
        -- Special case: both are instants
        when (earlier_finish = earlier_start) and (later_finish = later_start) then
          case
            when earlier_start = later_start then true
            else                                  false
          end

        -- Special case: earlier is instant
        when (earlier_finish = earlier_start) and (later_finish > later_start) then
          case
            when earlier_start = later_start then true
            else                                  false
          end

        -- Later is instant doesn't need special case treatment.

        -- Neither is instant
        else
          (earlier_start <= later_start) and (earlier_finish > later_start)
      end;
  begin
    assert (modeled_from_first_principles = modeled_using_ranges), 'Assert failed';
    return modeled_from_first_principles;
  end;
end;
$body$;
```

## The function modeled_overlaps_vs_overlaps(timestamp, timestamp, timestamp, timestamp) returns modeled_overlaps_vs_overlaps_result

This function returns a _user-defined type_ value that represents the return value from the native _overlaps_ operator together with the return value from _modeled_overlaps()_, for the same parameterization of the two durations, as a pair of _boolean_ values.

Create the type _modeled_overlaps_vs_overlaps_result_ and the function _modeled_overlaps_vs_overlaps)_ thus:

```plpgsql
drop type if exists modeled_overlaps_vs_overlaps_result cascade;

create type modeled_overlaps_vs_overlaps_result as (o boolean, m boolean);

drop function if exists modeled_overlaps_vs_overlaps(timestamp, timestamp, timestamp, timestamp) cascade;

create function modeled_overlaps_vs_overlaps(left_1 in timestamp, left_2 in timestamp, right_1 in timestamp, right_2 in timestamp)
  returns modeled_overlaps_vs_overlaps_result
  language plpgsql
as $body$
declare
  o     constant boolean not null :=                 (left_1, left_2) overlaps (right_1, right_2);
  m     constant boolean not null := modeled_overlaps(left_1, left_2,           right_1, right_2);
begin
  return (o, m)::modeled_overlaps_vs_overlaps_result;
end;
$body$;
```

## The function modeled_overlaps_vs_overlaps_display(timestamp, timestamp, timestamp, timestamp) returns text

This function creates a _text_ value from the parameterization of the two durations that encodes:

- the _left_1_, _left_2_, _right_1_, and _right_2_ plain _timestamp_ values
- the result, _true_ or _false_ from the native _overlaps_ operator
- only if the result from _modeled_overlaps()_ is different from the result from the native _overlaps_ operator, then it appends _DISAGREE_.

It also adds a significant test of its own. For each supplied parameterization, _(left_1, left_2, right_1, right_2)_, it invokes _modeled_overlaps_vs_overlaps()_ with each of the eight parameter orderings that supposedly express the same semantics:

- _(left_1, left_2, right_1, right_2)_
- _(left_2, left_1, right_1, right_2)_
- _(left_1, left_2, right_2, right_1)_
- _(left_2, left_1, right_2, right_1)_
- _(right_1, right_2, left_1, left_2)_
- _(right_2, right_1, left_1, left_2)_
- _(right_1, right_2, left_2, left_1)_
- _(right_2, right_1, left_2, left_1)_

and it tests that all of these invocations produce the same result.

First create the helper procedure _assert_r1_equals_r(int, modeled_overlaps_vs_overlaps_result, modeled_overlaps_vs_overlaps_result)_ to test if the result from the invocation that uses one of the second through the eighth orderings is identical to the result from the invocation that uses the first ordering.

```plpgsql
drop procedure if exists assert_r1_equals_r(int, modeled_overlaps_vs_overlaps_result, modeled_overlaps_vs_overlaps_result) cascade;

create procedure assert_r1_equals_r(test_nr in int, r1 in modeled_overlaps_vs_overlaps_result, r in modeled_overlaps_vs_overlaps_result)
  language plpgsql
as $body$
declare
  o1 constant boolean not null := r1.o;
  m1 constant boolean not null := r1.m;
  o  constant boolean not null :=  r.o;
  m  constant boolean not null :=  r.m;

  msg constant text not null := 'Test number '||test_nr::text||' assert failed';
begin
  assert ((o1 = o) and (m1 = m)), msg;
end;
$body$;
```

Now create the function _modeled_overlaps_vs_overlaps_display()_:

```plpgsql
drop function if exists modeled_overlaps_vs_overlaps_display(timestamp, timestamp, timestamp, timestamp) cascade;

create function modeled_overlaps_vs_overlaps_display(left_1 in timestamp, left_2 in timestamp, right_1 in timestamp, right_2 in timestamp)
  returns text
  language plpgsql
as $body$
declare
  input constant text      not null := rpad(left_1||', '  ::text, 29)||
                                       rpad(left_2        ::text, 27)||' |   ' ||
                                       rpad(right_1||', ' ::text, 29)||
                                       rpad(right_2       ::text, 27)||'   ';

  o1             boolean   not null := false;
  m1             boolean   not null := false;
  o              boolean   not null := false;
  m              boolean   not null := false;

  r              modeled_overlaps_vs_overlaps_result;
  r1             modeled_overlaps_vs_overlaps_result;
begin
  for j in 1..8 loop
    case j
      when 1 then
        r1 := modeled_overlaps_vs_overlaps(left_1, left_2, right_1, right_2);
        o1 := r1.o;
        m1 := r1.m;

      when 2 then
        r  := modeled_overlaps_vs_overlaps(left_2, left_1, right_1, right_2);
        call assert_r1_equals_r(1, r1, r);

      when 3 then
        r  := modeled_overlaps_vs_overlaps(left_1, left_2, right_2, right_1);
        call assert_r1_equals_r(2, r1, r);

      when 4 then
        r  := modeled_overlaps_vs_overlaps(left_2, left_1, right_2, right_1);
        call assert_r1_equals_r(3, r1, r);

      when 5 then
        r  := modeled_overlaps_vs_overlaps(right_1, right_2, left_1, left_2);
        call assert_r1_equals_r(4, r1, r);

      when 6 then
        r  := modeled_overlaps_vs_overlaps(right_2, right_1, left_1, left_2);
        call assert_r1_equals_r(5, r1, r);

      when 7 then
        r  := modeled_overlaps_vs_overlaps(right_1, right_2, left_2, left_1);
        call assert_r1_equals_r(6, r1, r);

      when 8 then
        r  := modeled_overlaps_vs_overlaps(right_2, right_1, left_2, left_1);
        call assert_r1_equals_r(7, r1, r);
    end case;

    return
      case (m1 = o1)
        when true then input||o1::text
        else           input||rpad(o1::text, 6)||'DISAGREE'
      end;
  end loop;
end;
$body$;
```

## The function modeled_overlaps_vs_overlaps_report() returns table(z text)

This table function simply invokes _modeled_overlaps_vs_overlaps_display()_ for all of the parameterizations that correspond to the interesting cases that are shown in the picture in this page's parent page in the section [_overlaps_ semantics in pictures](../#overlaps-semantics-in-pictures). It also adds a few more tests. Create it thus:

```plpgsql
drop function if exists modeled_overlaps_vs_overlaps_report() cascade;

create function modeled_overlaps_vs_overlaps_report()
  returns table(z text)
  language plpgsql
as $body$
begin
  z := 'TWO FINITE DURATIONS';                                                          return next;
  z := '--------------------';                                                          return next;
  z := '';                                                                              return next;

  z := ' 1. Durations do not overlap               '||
        modeled_overlaps_vs_overlaps_display
          ('2000-01-15', '2000-05-15', '2000-08-15', '2000-12-15');                     return next;

  z := ' 2. Right start = left end                 '||
        modeled_overlaps_vs_overlaps_display
          ('2000-01-15', '2000-05-15', '2000-05-15', '2000-12-15');                     return next;

  z := ' 3. Durations overlap                      '||
        modeled_overlaps_vs_overlaps_display
          ('2000-01-15', '2000-08-15', '2000-05-15', '2000-12-15');                     return next;

  z := ' 3. Durations overlap by 1 microsec        '||
        modeled_overlaps_vs_overlaps_display
          ('2000-01-15', '2000-06-15 00:00:00.000001', '2000-06-15', '2000-12-15');     return next;

  z := ' 3. Durations overlap by 1 microsec        '||
        modeled_overlaps_vs_overlaps_display
          ('2000-06-15', '2000-12-15', '2000-01-15', '2000-06-15 00:00:00.000001');     return next;

  z := ' 4. Contained                              '||
        modeled_overlaps_vs_overlaps_display
          ('2000-01-15', '2000-12-15', '2000-05-15', '2000-08-15');                     return next;

  z := ' 4. Contained, co-inciding at left         '||
        modeled_overlaps_vs_overlaps_display
          ('2000-01-15', '2000-06-15', '2000-01-15', '2000-08-15');                     return next;

  z := ' 4. Contained, co-inciding at right        '||
        modeled_overlaps_vs_overlaps_display
          ('2000-01-15', '2000-06-15', '2000-02-15', '2000-06-15');                     return next;

  z := ' 4. Durations coincide                     '||
        modeled_overlaps_vs_overlaps_display
          ('2000-01-15', '2000-06-15', '2000-01-15', '2000-06-15');                     return next;

  z := '';                                                                              return next;
  z := 'ONE INSTANT, ONE FINITE DURATION';                                              return next;
  z := '--------------------------------';                                              return next;
  z := '';                                                                              return next;

  z := ' 5. Instant before duration                '||
        modeled_overlaps_vs_overlaps_display
          ('2000-02-15', '2000-02-15', '2000-03-15', '2000-04-15');                     return next;

  z := ' 6. Instant coincides with duration start  '||
        modeled_overlaps_vs_overlaps_display
          ('2000-02-15', '2000-02-15', '2000-02-15', '2000-03-15');                     return next;

  z := ' 7. Instant within duration                '||
        modeled_overlaps_vs_overlaps_display
          ('2000-02-15', '2000-02-15', '2000-01-15', '2000-03-15');                     return next;

  z := ' 8. Instant coincides with duration end    '||
        modeled_overlaps_vs_overlaps_display
          ('2000-02-15', '2000-02-15', '2000-01-15', '2000-02-15');                     return next;

  z := ' 9. Instant after duration                 '||
        modeled_overlaps_vs_overlaps_display
          ('2000-05-15', '2000-05-15', '2000-03-15', '2000-04-15');                     return next;

  z := '';                                                                              return next;
  z := 'TWO INSTANTS';                                                                  return next;
  z := '------------';                                                                  return next;
  z := '';                                                                              return next;

  z := '10. Instants differ                        '||
        modeled_overlaps_vs_overlaps_display
          ('2000-01-15', '2000-01-15', '2000-06-15', '2000-06-15');                     return next;

  z := '11. Instants coincide                      '||
        modeled_overlaps_vs_overlaps_display
          ('2000-01-15', '2000-01-15', '2000-01-15', '2000-01-15');                     return next;
end;
$body$;
```

Invoke it thus:

```plpgsql
select z from modeled_overlaps_vs_overlaps_report();
```

This is the result:

```output
 TWO FINITE DURATIONS
 --------------------

  1. Durations do not overlap               2000-01-15 00:00:00,         2000-05-15 00:00:00         |   2000-08-15 00:00:00,         2000-12-15 00:00:00           false
  2. Right start = left end                 2000-01-15 00:00:00,         2000-05-15 00:00:00         |   2000-05-15 00:00:00,         2000-12-15 00:00:00           false
  3. Durations overlap                      2000-01-15 00:00:00,         2000-08-15 00:00:00         |   2000-05-15 00:00:00,         2000-12-15 00:00:00           true
  3. Durations overlap by 1 microsec        2000-01-15 00:00:00,         2000-06-15 00:00:00.000001  |   2000-06-15 00:00:00,         2000-12-15 00:00:00           true
  3. Durations overlap by 1 microsec        2000-06-15 00:00:00,         2000-12-15 00:00:00         |   2000-01-15 00:00:00,         2000-06-15 00:00:00.000001    true
  4. Contained                              2000-01-15 00:00:00,         2000-12-15 00:00:00         |   2000-05-15 00:00:00,         2000-08-15 00:00:00           true
  4. Contained, co-inciding at left         2000-01-15 00:00:00,         2000-06-15 00:00:00         |   2000-01-15 00:00:00,         2000-08-15 00:00:00           true
  4. Contained, co-inciding at right        2000-01-15 00:00:00,         2000-06-15 00:00:00         |   2000-02-15 00:00:00,         2000-06-15 00:00:00           true
  4. Durations coincide                     2000-01-15 00:00:00,         2000-06-15 00:00:00         |   2000-01-15 00:00:00,         2000-06-15 00:00:00           true

 ONE INSTANT, ONE FINITE DURATION
 --------------------------------

  5. Instant before duration                2000-02-15 00:00:00,         2000-02-15 00:00:00         |   2000-03-15 00:00:00,         2000-04-15 00:00:00           false
  6. Instant coincides with duration start  2000-02-15 00:00:00,         2000-02-15 00:00:00         |   2000-02-15 00:00:00,         2000-03-15 00:00:00           true
  7. Instant within duration                2000-02-15 00:00:00,         2000-02-15 00:00:00         |   2000-01-15 00:00:00,         2000-03-15 00:00:00           true
  8. Instant coincides with duration end    2000-02-15 00:00:00,         2000-02-15 00:00:00         |   2000-01-15 00:00:00,         2000-02-15 00:00:00           false
  9. Instant after duration                 2000-05-15 00:00:00,         2000-05-15 00:00:00         |   2000-03-15 00:00:00,         2000-04-15 00:00:00           false

 TWO INSTANTS
 ------------

 10. Instants differ                        2000-01-15 00:00:00,         2000-01-15 00:00:00         |   2000-06-15 00:00:00,         2000-06-15 00:00:00           false
 11. Instants coincide                      2000-01-15 00:00:00,         2000-01-15 00:00:00         |   2000-01-15 00:00:00,         2000-01-15 00:00:00           true
```

This is the same output that was shown on this page's parent page in the section [Two implementations that model the 'overlaps' semantics and that produce the same results](../#two-implementations-that-model-the-overlaps-semantics-and-that-produce-the-same-results).
