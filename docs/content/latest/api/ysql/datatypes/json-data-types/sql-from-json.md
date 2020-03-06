---
title: SQL from JSON
linktitle: SQL from JSON
summary: SQL from JSON
description: Create a SQL value from a JSON value
menu:
  latest:
    identifier: sql-from-json
    parent: functions-operators
isTocNested: true
showAsideToc: true
---

| operator/function | description |
| ---- | ---- |
| `::text` | Typecasts a `jsonb`  value to a SQL `text` value that conforms to RFC 7159. Single spaces (but not newlines) are inserted in conventioanally defined places. |
| `->>` | Like `->` except that the targeted value is returned as a SQL `text` value: _either_ the `::text` typecast of a compound JSON value; _or_ a typecastable `text` value holding the actual value that a primitive JSON value represents. |
| `#>>` | Like `->>` except that the to-be-read JSON subvalue is specified by the path to it from the enclosing JSON value. |
| `jsonb_extract_path_text()` | Functionally equivalent to the `#>>` operator. Parameterized in the same way as `jsonb_extract_path` and `json_extract_path`. There seems to be no reason to prefer the function form to the operator form. |
| `jsonb_populate_record()` | This function requires that the supplied JSON value is an _object_. It translates the JSON _object_ into the equivalent SQL record whose type name is supplied to the functions (using the strange locution `null::type_identifier`). |
| `jsonb_populate_recordset()` | A natural extension of the functionality of `jsonb_populate_record`. Requires that the supplied JSON value is an _array_, each of whose subvalues is an _object_ which is compatible with the specified SQL record data type, defined as a `type` whose name is passed using the locution `null:type_identifier`. |
| `jsonb_to_record()` | Syntax variant of the same functionality that  `jsonb_populate_record` provides. It has some quirky limitations when the input JSON value has JSON key-value pairs whose JSON values that are compound. It seems, therefore, to bring no practical advantage over its less restricted equivalent. |
| `jsonb_to_recordset()` | Bears the same relationship to `jsonb_to_record()` as  `json_populate_recordset()` bears to `json_populate_record()`. Again, it seems, therefore, to bring no practical advantage over their its restricted equivalent. |
| `jsonb_array_elements()` | Require that the supplied JSON value is an _array_ whose elements are primitive JSON values, and transform the list into a table whose single column has data type `text` and whose values are the `::text` typecasts of the primitive JSON values. It is the counterpart, for an _array_ of primitive JSON values, to `jsonb_populate_recordset()` for JSON _objects_. |
| `jsonb_array_elements_text()` | Bears the same relationship to `jsonb_array_elements` that the other `*text` functions bear to their plain counterparts: it's the same relationship that the `->>` and `#>>` operators bear, respectively to `->` and `#>`. |
| `jsonb_each()` | Requires that the supplied JSON value is an _object_. They return a row set with columns _"key"_ (as a SQL `text`) and _"value"_ (as a SQL `jsonb`). |
| `jsonb_each_text()` | Bears the same relationship to the result of `jsonb_each()` as does the result of the `->>` operator to that of the `->` operator. For that reason, `jsonb_each_text()` is useful when the results are primitive values. |
| `jsonb_pretty()` | Formats the text representation of the input JSON value  using whitespace to make it maximally easily human readable. |
