---
title: Typecast operators
linkTitle: Typecast operators - `::jsonb`, `::json` and `::text`
summary: Typecast operators - `::jsonb`, `::json` and `::text`
description: Typecast operators - `::jsonb`, `::json` and `::text`
menu:
  latest:
    identifier: typecast-operators
    parent: functions-operators
isTocNested: true
showAsideToc: true
---


Consider this round trip:

```
new_rfc_7159_text := (orig_rfc_7159_text::jsonb)::text
```

The round trip is, in general, literally, but not semantically, lossy because `orig_rfc_7159_text` can contain arbitrary occurrences of whitespace characters but `new_rfc_7159_text` has conventionally defined whitespace use: there are no newlines; and single spaces are used according to a rule.

```postgresql
do $body$
declare
  orig_rfc_7159_text constant text := '
    {
      "a": 1,
      "b": {"x": 1, "y": 19},
      "c": true
    }';

  expected_new_rfc_7159_text constant text :=
    '{"a": 1, "b": {"x": 1, "y": 19}, "c": true}';

  j constant jsonb := orig_rfc_7159_text::jsonb;
  new_rfc_7159_text constant text := j::text;
begin
  assert
    new_rfc_7159_text = expected_new_rfc_7159_text,
 'assert failed';
end;
$body$;
```

In contrast, this:

```
new_rfc_7159_text := (orig_rfc_7159_text::jsonb)::text
```

is literally non-lossy because `json` stores the actual Unicode text, as is, that defines the JSON value.

```postgresql
do $body$
declare
  orig_rfc_7159_text constant text := '
    {
      "a": 1,
      "b": {"x": 1, "y": 19},
      "c": true
    }';

  j constant json := orig_rfc_7159_text::json;
  new_rfc_7159_text constant text := j::text;

begin
  assert
    new_rfc_7159_text = orig_rfc_7159_text,
 'assert failed';
end;
$body$;
```
