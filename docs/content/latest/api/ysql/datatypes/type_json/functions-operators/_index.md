---
title: Functions and operators
linkTitle: Functions & operators
summary: Functions and operators
headerTitle: JSON functions and operators
description: The JSON functions and operators available in YugabyteDB are categorized below by purpose, that is based upon the goals you want to accomplish.
image: /images/section_icons/api/ysql.png
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

**Notes:** for an alphabetical listing of the JSON functions and operators, see the listing in the navigation bar.

There are two trivial typecast operators for converting between a `text` value that conforms to [RFC 7159](https://tools.ietf.org/html/rfc7159) and a `jsonb` or `json` value, the ordinarily overloaded `=` operator, 12 dedicated JSON operators, and 23 dedicated JSON functions.

Most of the operators are overloaded so that they can be used on both `json` and `jsonb` values. When such an operator reads a subvalue as a genuine JSON value, then the result has the same data type as the input. When such an operator reads a subvalue as a SQL `text` value that represents the JSON value, then the result is the same for a `json` input as for a `jsonb` input.

Some of the functions have just a `jsonb` variant and a couple have just a `json` variant. Function names reflect this by starting with `jsonb_` or ending with `_jsonb` (and, correspondingly, for the `json` variants). The reason that this naming convention is used, rather than ordinary overloading, is that YSQL can distinguish between same-named functions when the specification of their formal parameters differ but not when their return types differ. Some of the JSON builtin functions for a specific purpose differ only by returning a `json` value or a `jsonb` value. This is why a single consistent naming convention — a `b` variant and a plain variant — is used throughout.

When an operator or function has both a JSON value input and a JSON value output, the `jsonb` variant takes a `jsonb` input and produces a `jsonb` output; and, correspondingly, the `json` variant takes a `json` input and produces a `json` output. You can use the `ysqlsh` [`\df`](../../../../../admin/ysqlsh/#df-antws-pattern) metacommand to show the signature (that is, the data types of the formal parameters and the return value) of any of the JSON functions; but you cannot do this for the operators.

Check the full account of each to find its variant status. When an operator or function has both a `jsonb` and `json` variant, then only the `jsonb` variant is described. The functionality of the `json` variant can be trivially understood from the account of the `jsonb` functionality.

To avoid clutter in the tables, only the `jsonb` variants of the function names are mentioned except where only a `json` variant exists.

## Convert a SQL value to a JSON value

| Function or operator | Description |
| ---- | ---- |
| [`::jsonb`](./typecast-operators/#) | `::jsonb` typecasts a SQL `text` value that conforms to RFC 7159 to a `jsonb` value. Use the appropriate one of  `::jsonb`, `::json`, or `::text` to typecast between any pair out of `text`, `json`, and `jsonb`, in the direction that you need. |
| [`to_jsonb()`](./to-jsonb/) | Convert a single SQL value of any primitive or compound data type, that allows a JSON representation, to a sematically equivaent `jsonb`, or `json`, value. |
| [`row_to_json()`](./row-to-json/) | Create a JSON _object_ from a SQL _record_. It has no practical advantage over `to_jsonb()`. |
| [`array_to_json()`](./array-to-json/) | Create a JSON _array_ from a SQL _array_. It has no practical advantage over `to_jsonb()`. |
| [`jsonb_build_array()`](./jsonb-build-array/) | Create a JSON _array_ from a variadic list of _array_ values of arbirary SQL data type. |
| [`jsonb_build_object()`](./jsonb-build-object/) | Create a JSON _object_ from a variadic list that specifies keys with values of arbitrary SQL data type. |
| [`jsonb_object()`](./jsonb-object/)  | create a JSON _object_ from SQL _array_(s) that specifiy keys with their values of SQL data type `text`. |

## Convert a JSON value to another JSON value

| Function or operator | Description |
| ---- | ---- |
| [`->`](./subvalue-operators/) | Read the value specified by a one-step path returning it as a `json` or `jsonb` value.  |
| [`#>`](./subvalue-operators/) | Read the value specified by a multi-step path returning it as a `json` or `jsonb` value. |
| [&#124;&#124;](./concatenation-operator/) | Concatenate two `jsonb` values. The rule for deriving the output value depends upon the JSON data types of the operands. |
| [`-`](./remove-operators/) | Remove key-value pair(s) from an _object_ or a single value from an _array_. |
| [`#-`](./remove-operators) | Remove a single key-value pair from an _object_ or a single value from an _array_ at the specified path. |
| [`jsonb_extract_path()`](./jsonb-extract-path/) | Provide the identical functionality to the `#>` operator. The path is presented as a variadic list of steps that must all be `text` values. Its invocation more verbose than that of the `#>` operator and there is no reason to prefer the function form to the operator form. |
| [`jsonb_strip_nulls()`](./jsonb-strip-nulls/) | Find all key-value pairs at any depth in the hierarchy of the supplied JSON compound value (such a pair can occur only as an element of an _object_) and return a JSON value where each pair whose value is _null_ has been removed. |
| [`jsonb_set()` and `jsonb_insert()`](./jsonb-set-jsonb-insert/) | Use `jsonb_set()` to change an existing JSON value, i.e. the value of an existing key-value pair in a JSON _object_ or the value at an existing index in a JSON array. Use `jsonb_insert()` to insert a new value, either as the value for a key that doesn't yet exist in a JSON _object_ or beyond the end or before the start of the index range for a JSON _array_. |

## Convert a JSON value to a SQL value

| Function or operator | Description |
| ---- | ---- |
| [`::text`](./typecast-operators/) | Typecast a `jsonb`  value to a SQL `text` value that conforms to RFC 7159. Whitesace is conventioanally defined for a `jsonb` operand. Whitespace, in general, is unpredicatable for a `json` operand. |
| [`->>`](./subvalue-operators/) | Like `->` except that the targeted value is returned as a SQL `text` value: _either_ the `::text` typecast of a compound JSON value; _or_ a typecastable `text` value holding the actual value that a primitive JSON value represents. |
| [`#>>`](./subvalue-operators/) | Like `->>` except that the to-be-read JSON subvalue is specified by the path to it from the enclosing JSON value. |
| [`jsonb_extract_path_text()`](./jsonb-extract-path-text/) | Provide the identical functionality to the `#>>` operator. There is no reason to prefer the function form to the operator form. |
| [`jsonb_populate_record()`](./jsonb-populate-record/) | Convert a JSON _object_ into the equivalent SQL `record`. |
| [`jsonb_populate_recordset()`](./jsonb-populate-recordset/) | Convert a homogeneous JSON _array_ of JSON _objects_ into the equivalent set of SQL _records_. |
| [`jsonb_to_record()`](./jsonb-to-record/) | Convert a JSON _object_ into the equivalent SQL `record`. Syntax variant of the functionality that  `jsonb_populate_record()` provides. It has some restrictions and brings no practical advantage over its less restricted equivalent. |
| [`jsonb_to_recordset()`](./jsonb-to-recordset/) | Bears the same relationship to `jsonb_to_record()` as  `jsonb_populate_recordset()` bears to `jsonb_populate_record()`. Therefore, it brings no practical advantage over its restricted equivalent. |
| [`jsonb_array_elements()`](./jsonb-array-elements/) | Transform the JSON values of JSON _array_ into a SQL table of (i.e. `setof`) `jsonb` values. |
| [`jsonb_array_elements_text()`](./jsonb-array-elements-text/) | Transform the JSON values of JSON _array_ into a SQL table of (i.e. `setof`) `text` values. |
| [`jsonb_each()`](./jsonb-each/) | Create a row set with columns _"key"_ (as a SQL `text`) and _"value"_ (as a SQL `jsonb`) from a JSON _object_. |
| [`jsonb_each_text()`](./jsonb-each-text/) | Create a row set with columns _"key"_ (as a SQL `text`) and _"value"_ (as a SQL `text`) from a JSON _object_. |
| [`jsonb_pretty()`](./jsonb-pretty/) | Format the text representation of the JSON value that the input `jsonb` actual argument represents, using whitespace, to make it maximally easily human readable. |

## Get a property of a JSON value

| Function or operator | Description |
| ---- | ---- |
| [`=`](./equality-operator/) | Test if two `jsonb` values are equal. There is no `json` overload.|
| [`@>` and `<@`](./containment-operators/) | The `@>` operator tests if the left-hand JSON value contains the right-hand JSON value. The `<@` operator tests if the right-hand JSON value contains the left-hand JSON value. |
| [?, ?&#124;, and ?&](./key-existence-operators/) | Test for existence of keys.  Returns a SQL `boolean`. |
| [`jsonb_array_length()`](./jsonb-array-length/) | Return the count of values (primitive or compound) in the array. You can use this to iterate over the elements of a JSON _array_ using the  `->` operator. |
| [`jsonb_typeof()`](./jsonb-typeof/) | Return the data type of the JSON value as a SQL `text` value. |
| [`jsonb_object_keys()`](./jsonb-object-keys/) | Transform the list of key names int the supplied JSON _object_ into a set (i.e. table) of `text` values. |
