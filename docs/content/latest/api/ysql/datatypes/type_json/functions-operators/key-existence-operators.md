---
title: '?, ?|, and ?& (key existence)'
linkTitle: '?, ?|, and ?& (key existence)'
summary: Existence of keys
headerTitle: '?, ?|, and ?& (key existence)'
description: The ?, ?|, and ?&amp; (key existence) operators require that inputs are presented as jsonb value. They dont have overloads for json.
menu:
  latest:
    identifier: key-existence-operators
    parent: functions-operators
    weight: 17
isTocNested: true
showAsideToc: true
---

**Purpose:** test if the left-hand JSON value is an _object_ with a key-value pair whose _key_ whose name(s) are  given by the right-hand expression.

**Notes:** these operators require that inputs are presented as `jsonb` value. They don't have overloads for `json`. The first variant allows a single `text` value to be provided. The second and third variants allow a list of `text` values to be provided. The second is the _or_ (any) flavor and the third is the _and_ (all) flavor.

### Existence of the provided `text` value as _key_ of key-value pair: `?`

**Purpose:** test if the left-hand JSON value is an _object_ with a key-value pair whose _key_ whose name is given by the right-hand  scalar `text` value.

**Signature:**
```
input values:       jsonb ? text
return value:       boolean
```

Here, the existence expression evaluates to `true` so the  `assert` succeeds:

```postgresql
do $body$
declare
  j constant jsonb := '{"a": "x", "b": "y"}';
  key constant text := 'a';
begin
  assert
    j ? key,
 'unexpected';
end;
$body$;
```

Here, the existence expression for this counter-example evaluates to `false` because the left-hand JSON value has `x` as a _value_ and not as a _key_.

````postgresql
do $body$
declare
  j constant jsonb := '{"a": "x", "b": "y"}';
  key constant text := 'x';
begin
  assert
    not (j ? key),
 'unexpected';
end;
$body$;
````

Here, the existence expression for this counter-example evaluates to `false` because the left-hand JSON value has the _object_ not at top-level but as the second subvalue in a top-level _array_:

````postgresql
do $body$
declare
  j constant jsonb := '[1, {"a": "x", "b": "y"}]';
  key constant text := 'a';
begin
  assert
    not (j ? key),
 'unexpected';
end;
$body$;
````

### Existence of at least one provided `text` value as  the key of key-value pair: `?|`

**Purpose:** test if the left-hand JSON value an _object_ with _at least one_ key-value pair where its key is present in the right-hand list of scalar `text` values.

**Signature:**
```
input values:       jsonb ?| text[]
return value:       boolean
```
Here, existence expression evalueates to `true`.
```postgresql
do $body$
declare
  j constant jsonb := '{"a": "x", "b": "y", "c": "z"}';
  key_list constant text[] := array['a', 'p'];
begin
  assert
    j ?| key_list,
 'unexpected';
end;
$body$;
```

Here, the existence expression for this counter-example evaluates to `false` because none of the `text` values in the right-hand array exists as the key of a key-value pair.

```postgresql
do $body$
declare
  j constant jsonb := '{"a": "x", "b": "y", "c": "z"}';
  key_list constant text[] := array['x', 'p'];
begin
  assert
    not (j ?| key_list),
 'unexpected';
end;
$body$;
```

(`x` exists only as a primitive _string_ value.)

### Existence of all the provided `text` values as keys of key-value pairs: `?&`

**Purpose:** test if the left-hand JSON value an _object_ where _every_ value in the right-hand list of ordinary scalar `text`(or ordinary `text`) values present as the key of a key-value pair.

**Signature:**
```
input values:       jsonb ?& text[]
return value:       boolean
```

Here, existence expression ecaluates to _true_:

```postgresql
do $body$
declare
  j constant jsonb := '{"a": "x", "b": "y", "c": "z"}';
  key_list constant text[] := array['a', 'b', 'c'];
begin
  assert
    j ?& key_list,
 'unexpected';
end;
$body$;
```

Here, the existence expression for this counter-example evaluates to `false` because `'z'` in the right-hand key list exists as the value of a key-value pair, but not as the key of such a pair.

```postgresql
do $body$
declare
  j constant jsonb := '{"a": "x", "b": "y", "c": "z"}';
  key_list constant text[] := array['a', 'b', 'z'];
begin
  assert
    not(j ?& key_list),
 'unexpected';
end;
$body$;
```
