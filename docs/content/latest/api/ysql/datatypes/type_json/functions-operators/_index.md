---
title: Functions and operators
linkTitle: Functions & operators
summary: Functions and operators
description: JSON functions and operators
menu:
  latest:
    identifier: functions-operators
    parent: api-ysql-datatypes-json
    weight: 30
isTocNested: true
showAsideToc: true
---

The JSON functions and operators available in YugabyteDB are categorized below by purpose, that is based upon the goals you want to accomplish. Click one of the following goals to jump to a table that includes relevant JSON functions and operators.

**What are you trying to do?**

- [**Convert a SQL value to a JSON value**](#convert-a-sql-value-to-a-json-value)
- [**Convert a JSON value to another JSON value**](#convert-a-json-value-to-another-json-value)
- [**Convert a JSON value to a SQL value**](#convert-a-json-value-to-a-sql-value)
- [**Get a property of a JSON value**](#get-a-property-of-a-json-value)

**Note:** For an alphabetical listing of the JSON functions and operators, see the listing in the navigation bar.

There are two trivial typecast operators for converting between a `text` value that conforms to [RFC 7159](https://tools.ietf.org/html/rfc7159) and a `jsonb` or `json` value, the ordinarily overloaded `=` operator, 12 dedicated JSON operators, and 23 dedicated JSON functions.

Most of the operators are overloaded so that they can be used on both `json` and `jsonb` values. When such an operator reads a subvalue as a genuine JSON value, then the result has the same data type as the input. When such an operator reads a subvalue as a SQL `text` value that represents the JSON value, then the result is the same for a `json` input as for a `jsonb` input.

Some of the functions have just a `jsonb` variant and a couple have just a `json` variant. Function names reflect this by starting with `jsonb_` or ending with `_jsonb` (and, correspondingly, for the `json` variants). This naming scheme might seem to reflect a strange design choice by the implementers of PostgreSQL. A trivial test shows that an overload pair _can_ be distinguished by the difference in formal parameter data type between `jsonb` and `json`. However, PostgreSQL doesn't allow an overload function pair to be distinguished by the difference in return data type. It seems, then, that the PostgreSQL implementers decided to use a single consistent naming convention — a `b` variant and a plain variant — both when the pair differ in the data type of the _input_ JSON value and when they differ in the data type of the _output_ JSON value. Because YSQL is PostgreSQL-compatible, it follows the PostgreSQL convention.

When an operator or function has both a JSON value input and a JSON value output, the `jsonb` variant takes a `jsonb` input and produces a `jsonb` output; and, correspondingly, the `json` variant takes a `json` input and produces a `json` output. You can use the `ysqlsh` [`\df`](../../../../../admin/ysqlsh/#df-antws-pattern) metacommand to show the signature (that is, the data types of the formal parameters and the return value) of any of the JSON functions; but you cannot do this for the operators.

Check the full account of each to find its variant status. When an operator or function has both a `jsonb` and `json` variant, then only the `jsonb` variant is described. The functionality of the `json` variant can be trivially understood from the account of the `jsonb` functionality.

To avoid clutter in the tables, only the `jsonb` variants of the function names are mentioned except where only a `json` variant exists.

## Convert a SQL value to a JSON value

| Function or operator | Description |
| ---- | ---- |
| [`::jsonb`](./typecast-operators/#) | Typecasts SQL `text` value that conforms to RFC 7159 to a `jsonb` value. |
| [`to_jsonb()`](./to-jsonb/) | Converts a single SQL value into a semantically equivalent JSON value. The SQL value can be an arbitrary tree. The intermediate nodes are either `record` (which corresponds to a JSON _object_) or `array` (which corresponds to a JSON _array_). And the terminal nodes a primitive `text`, `numeric`, `boolean`, or `null` (which correspond, respectively, to JSON _string_, _number_, _boolean_, and _null_). In the general case, the result is a JSON _object_ or JSON _array_. In the degenerate case (where the input is a primitive SQL value) the result is the corresponding primitive JSON value. |
| [`row_to_json()`](./row-to-json/) | A special case of `to_json` that requires that the input is a SQL `record`. The result is a JSON _object_. It has no practical advantage over `to_jsonb()`. |
| [`array_to_json()`](./array-to-json/) | A special case of `to_json` that requires that the input is a SQL `array`. The result is a JSON _object_. It has no practical advantage over `to_jsonb()`. |
| [`jsonb_build_array()`](./jsonb-build-array/) | Variadic function that takes an arbitrary number of actual arguments of mixed SQL data types and produce a JSON _array_. Valuable because the values in a JSON _array_  can each have a different data type from the others, but the values in a SQL `array` must all have the same data type. |
| [`jsonb_build_object()`](./jsonb-build-object/) | Variadic function that is the obvious counterpart to `jsonb_build_array`. The keys and values can be specified in a few different ways, for example in an alternating list of keys (as `text` values) and their values (as values of any of `text`, `numeric`, `boolean` — or `null`. |
| [`jsonb_object()`](./jsonb-object/)  | Non-variadic function that achieves roughly the same effect as `jsonb_build_object()` with simpler syntax by presenting the key-value pairs using an array of data type `::text`. However, the functionality is severely limited because all the SQL `text` values are mapped to JSON _string_ values. |

## Convert a JSON value to another JSON value

| Function or operator | Description |
| ---- | ---- |
| [`->`](./jsonb-subvalue-operators/) | Reads a subvalue at a specified JSON _object_ key or JSON _array_ index as a JSON value.   |
| [`#>`](./jsonb-subvalue-operators/) | Like `->` except that the to-be-read JSON subvalue is specified by the path to it from the enclosing JSON value. |
| [&#124;&#124;](./concatenation-operator/) | Concatenates two JSON values to produce a new JSON value. |
| [`-`](./remove-operators/) | Creates a new JSON value from the input JSON value: _either_ by removing a key-value pair with the specified key from a JSON _object_; _or_ by removing a JSON value at the specified index in a JSON _array_. Error if the input is not a JSON _object_ or JSON _array_. |
| [`#-`](./remove-operators) | Like `-` except that the to-be-removed key-value pair (from a JSON _object_) or JSON value (from a JSON _array_) is specified by a path from the enclosing JSON value. The path is specified in the same way as for the `#>` operator. |
| [`jsonb_extract_path()`](./jsonb-extract-path/) | Functionally equivalent to the `#>` operator. The path is presented as a variadic list of steps that must all be `text` values. Its invocation more verbose than that of the `#>` operator and there seems to be no reason to prefer the function form to the operator form. |
| [`jsonb_strip_nulls()`](./jsonb-strip-nulls/) | Finds all key-value pairs at any depth in the hierarchy of the supplied JSON compound value (such a pair can occur only as an element of an _object_) and return a JSON value where each pair whose value is _null_. has been removed. By definition, they leave _null_ values within _arrays_ untouched. |
| [`jsonb_set()` and `jsonb_insert()`](./jsonb-set-jsonb-insert/) | These functions return a new JSON value modified from the input value in the specified way using the so-called replacement JSON value. Because the effect of `jsonb_set` is identical to that of `jsonb_insert` in some cases, they are grouped together here. However, in other cases, there are critical differences. They target a specific JSON value at a specified path. When the target is a key-value pair in a JSON _object_, they set it to the specified value when the key already exists and create it when it doesn't. When the target is a JSON value at a specified index in a JSON _array_, and the index already exists, they either set it or insert a new value after or before it. And when the index is before the _array_'s first value or after its last value, they insert it. |

## Convert a JSON value to a SQL value

| Function or operator | Description |
| ---- | ---- |
| [`::text`](./typecast-operators/) | Typecasts a `jsonb`  value to a SQL `text` value that conforms to RFC 7159. Single spaces (but not newlines) are inserted in conventioanally defined places. |
| [`->>`](./json-subvalue-operators/) | Like `->` except that the targeted value is returned as a SQL `text` value: _either_ the `::text` typecast of a compound JSON value; _or_ a typecastable `text` value holding the actual value that a primitive JSON value represents. |
| [`#>>`](./json-subvalue-operators/) | Like `->>` except that the to-be-read JSON subvalue is specified by the path to it from the enclosing JSON value. |
| [`jsonb_extract_path_text()`](./jsonb-extract-path-text/) | Functionally equivalent to the `#>>` operator. Parameterized in the same way as `jsonb_extract_path` and `json_extract_path`. There seems to be no reason to prefer the function form to the operator form. |
| [`jsonb_populate_record()`](./jsonb-populate-record/) | This function requires that the supplied JSON value is an _object_. It translates the JSON _object_ into the equivalent SQL record whose type name is supplied to the functions (using the strange locution `null::type_identifier`). |
| [`jsonb_populate_recordset()`](./jsonb-populate-recordset/) | A natural extension of the functionality of `jsonb_populate_record`. Requires that the supplied JSON value is an _array_, each of whose subvalues is an _object_ which is compatible with the specified SQL record data type, defined as a `type` whose name is passed using the locution `null:type_identifier`. |
| [`jsonb_to_record()`](./jsonb-to-record/) | Syntax variant of the same functionality that  `jsonb_populate_record` provides. It has some quirky limitations when the input JSON value has JSON key-value pairs whose JSON values that are compound. It seems, therefore, to bring no practical advantage over its less restricted equivalent. |
| [`jsonb_to_recordset()`](./jsonb-to-recordset/) | Bears the same relationship to `jsonb_to_record()` as  `json_populate_recordset()` bears to `json_populate_record()`. Again, it seems, therefore, to bring no practical advantage over their its restricted equivalent. |
| [`jsonb_array_elements()`](./jsonb-array-elements/) | Require that the supplied JSON value is an _array_ whose elements are primitive JSON values, and transform the list into a table whose single column has data type `text` and whose values are the `::text` typecasts of the primitive JSON values. It is the counterpart, for an _array_ of primitive JSON values, to `jsonb_populate_recordset()` for JSON _objects_. |
| [`jsonb_array_elements_text()`](./jsonb-array-elements-text/) | Bears the same relationship to `jsonb_array_elements` that the other `*text` functions bear to their plain counterparts: it's the same relationship that the `->>` and `#>>` operators bear, respectively to `->` and `#>`. |
| [`jsonb_each()`](./jsonb-each/) | Requires that the supplied JSON value is an _object_. They return a row set with columns _"key"_ (as a SQL `text`) and _"value"_ (as a SQL `jsonb`). |
| [`jsonb_each_text()`](./jsonb-each-text/) | Bears the same relationship to the result of `jsonb_each()` as does the result of the `->>` operator to that of the `->` operator. For that reason, `jsonb_each_text()` is useful when the results are primitive values. |
| [`jsonb_pretty()`](./jsonb-pretty/) | Formats the text representation of the input JSON value  using whitespace to make it maximally easily human readable. |

## Get a property of a JSON value

| Function or operator | Description |
| ---- | ---- |
| [`=`](./equality-operator/) | The `=` operator is overloaded for all the SQL data types including `jsonb`. By a strange oversight, there is _no overload_ for plain `json`. |
| [`@>` and `<@`](./containment-operators/) | `@>` tests if the left-hand JSON value contains the right-hand JSON value. And `<@` tests if the right-hand JSON value contains the left-hand JSON value. Returns a SQL `boolean`. |
| [?, ?&#124;, and ?&](./key-existence-operators/) | Test for existence of keys.  Returns a SQL `boolean`. |
| [`jsonb_array_length()`](./jsonb-array-length/) | The input must be a JSON _array_. Returns the number of JSON values in the _array_ as a SQL `int`. |
| [`jsonb_typeof()`](./jsonb-typeof/) | Takes a single JSON value of arbitrary data type (_string_, _number_, _boolean_, _null_,  _object_, and _array_) and returns the data type name as a SQL `text` value. |
| [`jsonb_object_keys()`](./jsonb-object-keys/) | Require that the supplied JSON value is an _object_. It transforms the list of key names into a set (i.e. table) of SQL `text` values. |
