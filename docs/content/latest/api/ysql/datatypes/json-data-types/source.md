# JSON data types and functionality

## Synopsis

JSON stands for JavaScript Object Notation, a text format for the serialization of structured data. Its syntax and semantics are defined in [RFC 7159](https://tools.ietf.org/html/rfc7159). JSON is represented by Unicode characters; and such a representation is usually called a document. Whitespace outside of _string_ values and _object_ keys (see below) is insignificant.

YSQL supports two data types to represent a JSON document: `json` and `jsonb`. Each rejects any JSON document that does not conform to RFC 7159. The `json` data type simply stores the text representation of a JSON document as presented. In contrast, the `jsonb` data type stores a parsed representation of the document hierarchy of subvalues in an appropriate internal format. Some people prefer the mnemonic _"binary"_ for the _"b"_ suffix; others prefer _"better"_. Of course, it takes more computation to store a JSON document as a `jsonb` value than as `json` value. This cost is repaid when subvalues are operated on using the operators and functions that this section describes.

JSON was invented as a data interchange format, initially to allow an arbitrary compound value in a JavaScript program to be serialized, transported as text, and then deserialized in another JavaScript program faithfully to reinstantiate the original compound value. Later, very many other programming languages—including, now, SQL and PL/pgSQL—support serialization to, and deserialization from, JSON. Moreover, it has become common to store JSON as the persistent representation of record in a table with just a primary key column and a `json` or `jsonb` column for facts that could be represented classically in a table design that conforms to the relational model. This pattern arose first in NoSQL databases but it is now widespread in SQL databases.

## Description

```
type_specification ::= { `JSON` | `JSONB` }
```

The remainder of the account of JSON data types and functionality is organized thus:

- The JSON data types
- Classification of operators and functions by purpose
- Creating indexes and check constraints on `json` and `jsonb` columns

## The JSON data types

JSON can represent (sub)values of four primitive data types and of two compound data types.

The primitive data types are _string_, _number_, _boolean_, and _null_. There is no way to declare the data type of a JSON value; rather, it simply emerges from the syntax of the representation. It is for this reason that, in the JSON type system, _null_ is defined as a data type rather than as a "value" (strictly, the absence of information about the value) of one of the other data types. Notice that JSON cannot represent date-time values except as a conventionally formatted _string_ value. 

The two compound data types are _object_, and _array_.

A manifest constant JSON value is represented in a SQL statement or a PL/pgSQL program by the `::json` or `::jsonb` typecast of a `text` value that conforms to RFC 7159.

### _String_

A JSON _string_ value is a sequence of zero, one, or many Unicode characters enclosed by the `"` character. Here are some examples, shown as SQL manifest constants:

```
'"Dog"'::jsonb
```

The empty _string_ is legal, and is distinct from the JSON _null_.

```
'""'::jsonb
```

Case and whitespace are significant. Special characters within a _string_ value need to be escaped, thus:

- Backspace: `\b`
- Form feed: `\f`
- Newline: `\n`
- Carriage return: `\r`
- Tab: `\t`
- Double quote: `\"`
- Backslash: `\\`

For example:

```
 '"\"First line\"\n\"second line\""'::jsonb
```

The explanation of the difference between the operators `->` and `->>` is illustrated nicely by this JSON _string_ value.

### _Number_

Here are some examples, shown as SQL manifest constants:

```
'17'::jsonb
```

and:

```
'4.2'::jsonb
```

and:

```
'2.99792E8'::jsonb
```

Notice that JSON makes no distinction between integers and real numbers.

### _Boolean_ and _null_

Here are some examples, shown as SQL manifest constants:

```
'true'::jsonb
```

and:

```
'false'::jsonb
```

and:

```
'null'::jsonb
```

### _Object_

An _object_ is a set of key-value pairs separated by _commas_ and surrounded by _curly braces_. The order is insignificant. The values in an _object_ do not have to have the same data types as each other. For example:

```json
'{
  "a 1" : "Abc",
  "a 2" : 42,
  "a 3" : true,
  "a 4" : null,
  "a 5" : {"x" : 1, "y": "Pqr"}
}'::jsonb
```

Keys are case-sensitive and whitespace within such keys is significant. They can even contain characters that must be escaped. However, if a key does include spaces and special characters, the syntax that you need to read its value becomes rather complex. It would seem to be sensible, therefore, to avoid exploiting this freedom.

An object_ can include more than one key-value pair with the same key. This is not recommended, but the outcome is well-defined: the last-mentioned key-value pair, in left-to-right order, in the set of key-value pairs with the same key "wins". You can test this by reading the value for a specified key with an operator like `->`.

### _Array_

An _array_ is an ordered list of unnamed JSON values  — in other words, the order is defined and is significant. The values in an _array_ do not have to have the same data types as each other. For example:

```
'[1, 2, "Abc", true, false, null, {"x": 17, "y": 42}]'::jsonb
```
The values in an _array_ are indexed from `0`. See the account of the `->` operator.

### Example compound JSON value

```json
{
  "given_name"         : "Fred",
  "family_name"        : "Smith",
  "email_address"      : "fred@yb.com",
  "hire_date"          : "17-Jan-2015",
  "job"                : "sales",
  "base_annual_salary" : 50000,
  "commisission_rate"  : 0.05,
  "phones"             : ["+11234567890", "+13216540987"]
}
```

This is a JSON _object_ with eight fields. The first seven are primitive _string_ or _number_ values (one of which conventionally represents a date) and the eighth is an array of two primitive _string_ values. The text representations of the phone numbers follow a convention by starting with + and  (presumably) a country code. JSON has no mechanisms for defining such conventions and for enforcing conformance.

_Note:_ the section _"Creating indexes and check constraints on `json` and `jsonb` columns"_, shows how these limitations can be ameliorated when a JSON document is stored in a column in a SQL table.

In general, the top-level JSON document is an arbitrarily deep and wide hierarchy of subdocuments whose leaves are primitive values.

Notably, and in contrast to XML, JSON is not self-describing. Moreover, JSON does not support comments. But the intention is that the syntax should be reasonably intuitively obvious and human-readable.

It is the responsibility of the consumers of JSON documents to discover the composition rules of any corpus that they have to deal with by _ad hoc_ methods—at best external documentation and at worst human (or mechanical) inspection.

Most programming languages have data types that correspond directly to JSON's primitive data types and to its compound _object_ and _array_ data types.

Because YSQL manipulates a JSON document as the value of a `json` or `jsonb` table-row intersection or PL/pgSQL variable, the terms "`json` [sub]value" or "`jsonb` [sub]value" (and JSON value as the superclass) are preferred from now on in this section—using "value" rather than "document".







## Creating indexes and check constraints on JSON columns

Often, when  JSON documents are inserted into a table, the table will have just the columns `k` (as a self-populating surrogate primary key) and `v` of data type `jsonb`. This choice allows the use of a broader range of operators and functions, and allows these to execute more efficiently, then when a `json` column is used.

It's most likely that each document will be a JSON _object_ and that all will conform to the same structure definition. In other words, each _object_ will have the same set of possible key names (but some might be missing) and the same JSON data type for the value for each key. And when a data type is compound, the same notion of common structure definition will apply, extending the notion recursively to arbitrary depth. Here is an example. To reduce clutter, primary key is not defined to be self-populating. 

```postgresql
create table books(k int primary key, json_doc jsonb not null);

insert into books(k, json_doc) values
  (1,
  '{ "ISBN"   " 1234567890123",
     "title"  : "Macbeth", 
     "author" : {"given_name": "William", "family_name": "Shakespeare"},
     "year"   : 1623}'),

  (2,
  '{ "title"  : "Hamlet",
     "author" : {"given_name": "William", "family_name": "Shakespeare"},
     "year"   : 1603,
     "editors": ["Lysa", "Elizabeth"] }'),

  (3,
  '{ "title"  : "Oliver Twist",
     "author" : {"given_name": "Charles", "family_name": "Dickens"},
     "year"   : 1838,
     "genre"  : "novel",
     "editors": ["Mark", "Tony", "Britney"] }'),
  (4,
  '{ "title"  : "Great Expectations",
     "author" : {"family_name": "Dickens"},
     "year"   : 1950,
     "genre"  : "novel",
     "editors": ["Robert", "John", "Melisa", "Elizabeth"] }'),

  (5,
  '{ "title"  : "A Brief History of Time",
     "author" : {"given_name": "Stephen", "family_name": "Hawking"},
     "year"   : 1988,
     "genre"  : "science",
     "editors": ["Melisa", "Mark", "John", "Fred", "Jane"] }'),

  (6,
  '{
    "year": 1989,
    "genre": "novel",
    "title": "Joy Luck Club",
    "author": {"given_name": "Amy", "family_name": "Tan"},
    "editors": ["Ruilin", "Jueyin"]}');
```

Some of the rows have some of the keys missing. But the row with `k = 6` has every key.

You will probably want at least to know if your corpus contains a non-conformant document and, in some cases, that you will want to disallow non-conformant documents.

You will almost certainly want to retrieve documents, not simply by providing the key, but rather by using predicates on their content—the primitive values that they contain. You will probably want, also, to project out values of interest.

For example, a probable query will be "Show me the books whose hire publivation year is between  and whose phone number list includes a _US_ number with area code _415_. 

Of course, then, you will want these queries to be supported by indexes. (The alternative—a table scan over a huge corpus where each document is analyzed on the fly to evaluate the selection predicates—is simply unworkable.)

### Indexes on _jsonb_ columns

Coming soon.

### Check constraints  on _jsonb_ columns

Coming soon.

# Here is the toilet roll of individual accounts

### The typecast operators, `::jsonb`, `::json` and `::text`

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

## Operators for reading JSON subvalues

### The `->` operator

The general form is:

_JSON value_ `->` _key_

It requires that the JSON value is an _object_ or an _array_. _Key_ is a SQL value.

When _key_ is a SQL text value, it reads the JSON value of the key-value pair with that key from an _object_. If the input JSON value is `json`, then the output JSON value is `json`., and correspondingly if the input JSON value is `jsonb`. For example:

```postgresql
do $body$
declare
  j  constant jsonb := '{"a": 1, "b": {"x": 1, "y": 19}, "c": true}';
  jsub constant jsonb := j  -> 'b';
  expected_jsub constant jsonb := '{"x": 1, "y": 19}';
begin
  assert jsub = expected_jsub, 'unexpected';
end;
$body$;
```

And when _key_ is a SQL integer value, it reads the so-indexed JSON value from an _array_. The first value in an _array_ has the index `0`. For example:

```postgresql
do $body$
declare
  j  constant jsonb := '["a", "b", "c", "d"]';
  j_first constant jsonb := j -> 0;
  expected_first constant jsonb := '"a"';
begin
  assert
    j_first = expected_first, 'unexpected';
end;
$body$;
```

See the account of the `jsonb_array_length()` function.

### The `#>` operator

An arbitrarily deeply located JSON subvalue is identified by its path from the topmost JSON value. In general, a path is specified by a mixture of keys for _object_ subvalues and index values for _array_ subvalues. Consider this JSON value:

```json
[
  1,
  {
    "x": [
      1,
      true,
      {"a": "cat", "b": "dog"},
      3.14159
    ],
    "y": true
  },
  42
]
```

- At the topmost level of decomposition, it's an _array_ of three subvalues.

- At the second level of decomposition, the second _array_ subvalue is an _object_ with two key-value pairs called `"x"` and `"y"`.

- At the third level of decomposition, the subvalue for the key `"x"` is an _array_ of subvalues.

- At the fourth level of decomposition, the third _array_ subvalue is an _object_ with two key-value pairs called `"a"` and `"b"`.

- And at the fifth level of decomposition, the subvalue for key `"b"` is the primitive _string_ value `"dog"`.

This, therefore, is the path to the primitive JSON _string_ value `"dog"`:

```
-> 1 -> 'x' -> 2 -> 'b'
```

(Recall that _array_ value indexing starts at _zero_.) 

The `#>` operator is a convenient syntax sugar shorthand for specifying a long path compactly, thus:
```
#> array['1', 'x', '2', 'b']::text[]
```
Notice that with the `->` operator, integers must be presented as such (so that `'1'` rather than `1` would, surprisingly, read out `null`. However, with the `#>` operator, integers must be presented as convertible `text` values because all the values in a SQL array must have the same data type.

The PL/pgSQL `assert` confirms that both the `->` path specification and the `#>` path specification produce the same result, thus:

```postgresql
do $body$
declare
  j constant jsonb := '
    [
      1,
      {
        "x": [
          1,
          true,
          {"a": "cat", "b": "dog"},
          3.14159
        ],
        "y": true
      },
      42
    ]';

  one constant int := 1;
  two constant int := 2;
  x constant text := 'x';
  b constant text := 'b';
  one_t constant text := '1';
  two_t constant text := '2';

  jsub_1 constant jsonb := j -> one -> x -> two -> b;
  jsub_2 constant jsonb := j #> array[one_t, x, two_t, b];

  expected_jsub constant jsonb := '"dog"';
begin
  assert
    (jsub_1 = expected_jsub) and
    (jsub_2 = expected_jsub),
  'unexpected';
end;
$body$;
```

The paths are written using PL/pgSQL variables so that, as a pedagogic device, the data types are explicit.

### The `->>` and `#>>` operators

The `->` operator returns a JSON object. When the targeted value is compound, the `->>` operator returns the `::text` typecast of the value. But when the targeted value is primitive, the `->>` operator returns the value itself, as a `text` value. In particular; a JSON _number_ value is returned as the `::text` typecast of that value (for example `'4.2'`), allowing it to be trivially `::numeric` typecasted back to what it actually is; a JSON _boolean_ value is returned as the `::text` typecast of that value (`'true'` or `'false'`), allowing it to be trivially `::boolean` typecasted back to what it actually is; a JSON _string_ value is return as is as a `text` value; and a JSON _null_ value is returned as a genuine SQL `null` so that the `is null` test is `true`.

The difference in semantics between the `->` operator and the `->>` operator is vividly illustrated (as promised above) by targeting this primitive JSON _string_ subvalue:

```
"\"First line\"\n\"second line\""
```

from the JSON value in which it is embedded. For example, here it is the value of the key `"a"` in a JSON _object_:

```postgresql
do $body$
declare
  j constant jsonb := '{"a": "\"First line\"\n\"second line\""}'::jsonb;

  a_value_j constant jsonb := j ->  'a';
  a_value_t constant text  := j ->> 'a';

  expected_a_value_j constant jsonb :=
    '"\"First line\"\n\"second line\""'::jsonb;
  
  expected_a_value_t constant text := '"First line"
"second line"';

begin
  assert
    (a_value_j = expected_a_value_j) and
    (a_value_t = expected_a_value_t),
  'unexpected';
end;
$body$;
```

Understanding the difference between the `->` operator and the `->>` operator completely informs the understanding of the difference between the `#>` operator and the `#>>` operator.

The `>>` variant (both for `->` _vs_ `->>` and for `#>` _vs_ `#>>`) is interesting mainly when the denoted subvalue is a primitive value (as the example above showed for a primitive _string_ value). If you read such a subvalue, it's most likely that you'll want to cast it to a value of the appropriate SQL data type, `numeric`, `text`, or `boolean`.  You might use your understanding of a value's purpose (for example _"quantity ordered"_ or _"product SKU"_) to cast it to, say, an `int` value or a constrained text type like `varchar(30)`. In contrast, if you read a compound subvalue, it's most likely that you'll want it as a genuine `json` or `jsonb` value.

### Summary: `->` versus `->>` and `#>` versus `#>>`

- The `->` and `#>` operators each return a genuine JSON value. When the input is `json`, the return value is `json`. And when the input is `jsonb`, the return value is `jsonb`.
- The `->>` and `#>>` operators each return a genuine `text` value, both when the input is `json`, and when the input is `jsonb`.
- If the value that the path denotes is a compound JSON value, then the` >>` variant returns the `text` representation of the JSON value, as specified by RFC 7159. (The designers of the PostgreSQL functionality had no other feasible choice.) But if the JSON value that the path denotes is primitive, then the >> variant produces the text representation of the value itself. It turns out that the text representation of a primitive JSON value and the text representation of the value itself differ only for a JSON string—exemplified by `"a"` versus `a`.

The following `assert` test all these rules:

```postgresql
do $body$
declare
  ta constant text     := 'a';
  tn constant numeric  := -1.7;
  ti constant numeric  := 42;
  tb constant boolean  := true;
  t2 constant text  := '["a", -1.7, 42, true, null]';

  j1 constant jsonb := '"a"';
  j2 constant jsonb := t2::jsonb;
  j3 constant jsonb := '{"p": 1, "q": ["a", -1.7, 42, true, null]}';
begin
  assert
    (j2 ->  0)               = j1 and
    (j2 ->> 0)               = ta and
    (j2 ->> 1)::numeric      = tn and
    (j2 ->> 2)::int          = ti and
    (j2 ->> 3)::boolean      = tb and
    (j2 ->> 4)            is null and

    (j3 #>  array['q', '0']) = j1 and
    (j3 #>> array['q', '0']) = ta and

    (j3 ->  'q')             = j2 and
    (j3 ->> 'q')             = t2
    ,
    'assert failed';
end;
$body$;
```

## Reporting a property of a JSON value

### Equality: the `=` operator

This operator requires that the inputs are presented as `jsonb` values. It doesn't have an overload for `json`.

```postgresql
do $body$
declare
  j1 constant jsonb := '["a","b","c"]';
  j2 constant jsonb := '
    [
      "a","b","c"
    ]';
begin
  assert
    j1::text = j2::text,
  'unexpected';
end;
$body$;
```

Notice that the text forms of the to-be-compared JSON values may differ in whitespace. Because `jsonb` holds a fully parsed representation of the value, whitespace (exception within primitive JSON _string_ values) has no meaning.

If you need to test two `json` values for equality, then you must `::text` typecast each.

```postgresql
do $body$
declare
  j1 constant json := '["a","b","c"]';
  j2 constant json := '["a","b","c"]';
begin
  assert
    j1::text = j2::text,
  'unexpected';
end;
$body$;
```

### Containment: the `@>` and `<@` operators

`The @>` operator tests if the left-hand JSON value contains the right-hand JSON value. And the `<@` operator tests if the right-hand JSON value contains the left-hand JSON value. These operators require that the inputs are presented as `jsonb` values. They don't have overloads for `json`. 

```postgresql
do $body$
declare
  j_left  constant jsonb := '{"a": 1, "b": 2}';
  j_right constant jsonb := '{"b" :2}';
begin
  assert
    (j_left @> j_right) and
    (j_right <@ j_left),
 'assert failed';
end;
$body$;
```

### Existence of keys: the `?`, `?|`, and `?&` operators

These operators require that inputs are presented as `jsonb` value. They don't have overloads for `json`. The first variant allows a single `text` value to be provided. The second and third variants allow a list of `text` values to be provided. The second is the _or_ (any) flavor and the third is the _and_ (all) flavor.

#### Existence of the provided `text` value as _key_ of key-value pair: `?`
Is the left-hand JSON value an _object_ with a key-value pair whose _key_ whose name is given by the right-hand  scalar `text` value? The existence expression evaluates to `true` so the  `assert` succeeds:

```postgresql
do $body$
declare
  j constant jsonb := '{"a": "x", "b": "y"}';
  key constant text := 'a';
begin
  assert
    j ? key,
 'assert failed';
end;
$body$;
```

The existence expression for this counter-example evaluates to `false` because the left-hand JSON value has `x` as a _value_ and not as a _key_.

```postgresql
do $body$
declare
  j constant jsonb := '{"a": "x", "b": "y"}';
  key constant text := 'x';
begin
  assert
    not (j ? key),
 'assert failed';
end;
$body$;
```

The existence expression for this counter-example evaluates to `false` because the left-hand JSON value has the _object_ not at top-level but as the second subvalue in a top-level _array_:

```postgresql
do $body$
declare
  j constant jsonb := '[1, {"a": "x", "b": "y"}]';
  key constant text := 'a';
begin
  assert
    not (j ? key),
 'assert failed';
end;
$body$;
```

#### Existence of at least one provided `text` value as  the key of key-value pair: `?|`

Is the left-hand JSON value an _object_ with at least one key-value pair where its key is present in the right-hand list of scalar `text` values?

```postgresql
do $body$
declare
  j constant jsonb := '{"a": "x", "b": "y", "c": "z"}';
  key_list constant text[] := array['a', 'p'];
begin
  assert
    j ?| key_list,
 'assert failed';
end;
$body$;
```

The existence expression for this counter-example evaluates to `false` because none of the `text` values in the right-hand array exists as the key of a key-value pair.

```postgresql
do $body$
declare
  j constant jsonb := '{"a": "x", "b": "y", "c": "z"}';
  key_list constant text[] := array['x', 'p'];
begin
  assert
    not (j ?| key_list),
 'assert failed';
end;
$body$;
```

(`x` exists only as a primitive _string_ value.)

#### Existence of all the provided `text` values as keys of key-value pairs: `?&`

Is the left-hand JSON value an _object_ where every value in the right-hand list of ordinary scalar `text`(or ordinary `text`) values present as the key of a key-value pair?   This shows _true_:

```postgresql
do $body$
declare
  j constant jsonb := '{"a": "x", "b": "y", "c": "z"}';
  key_list constant text[] := array['a', 'b', 'c'];
begin
  assert
    j ?& key_list,
 'assert failed';
end;
$body$;
```

The existence expression for this counter-example evaluates to `false` because `'z'` in the right-hand key list exists as the value of a key-value pair, but not as the key of such a pair.

```postgresql
do $body$
declare
  j constant jsonb := '{"a": "x", "b": "y", "c": "z"}';
  key_list constant text[] := array['a', 'b', 'z'];
begin
  assert
    not(j ?& key_list),
 'assert failed';
end;
$body$;
```

## Operators for constructing _jsonb_ values

These operators require that the inputs are presented as `jsonb` values. They don't have overloads for `json`.

### Concatenation: the `||` operator

If both sides of the operator are primitive JSON values, then the result is an _array_ of these values:

```postgresql
do $body$
declare
  j_left constant jsonb := '17';
  j_right constant jsonb := '"x"';
  j_expected constant jsonb := '[17, "x"]';
begin
  assert
    j_left || j_right = j_expected,
 'assert failed';
end;
$body$;
```
If one side is a primitive JSON value and the other is an  _array_ , then the result is an _array_:
```
do $body$
declare
  j_left constant jsonb := '17';
  j_right constant jsonb := '["x", true]';
  j_expected constant jsonb := '[17, "x", true]';
begin
  assert
    j_left || j_right = j_expected,
 'assert failed';
end;
$body$;
```

If each side is an _object_, then the results is an _object_ with all the key-value pairs present:

```postgresql
do $body$
declare
  j_left constant jsonb := '{"a": 1, "b": 2}';
  j_right constant jsonb := '{"p":17, "a": 19}';
  j_expected constant jsonb := '{"a": 19, "b": 2, "p": 17}';
begin
  assert
    j_left || j_right = j_expected,
 'assert failed';
end;
$body$;
```

If the keys of key-value pairs collide, then the last-mentioned one wins, just as when the keys of such pairs collide in a single _object_:

```postgresql
do $body$
declare
  j_left constant jsonb := '{"a": 1, "b": 2}';
  j_right constant jsonb := '{"p":17, "a": 19}';
  j_expected constant jsonb := '{"a": 19, "b": 2, "p": 17}';
begin
  assert
    j_left || j_right = j_expected,
 'assert failed';
end;
$body$;
```

If one side is an _object_ and the other is an _array_, then the _object_ is absorbed as a value in the _array_:

```postgresql
do $body$
declare
  j_left constant jsonb := '{"a": 1, "b": 2}';
  j_right constant jsonb := '[false, 42, null]';
  j_expected constant jsonb := '[{"a": 1, "b": 2}, false, 42, null]';
begin
  assert
    j_left || j_right = j_expected,
 'assert failed';
end;
$body$;
```

### Remove single key-value pair from an _object_ or a single value from an _array_: the `-` operator

Notice that, for all the operators whose behavior is described by using the term "remove", this is a convenient shorthand. The actual effect of these operators is to create a _new_ `jsonb` value from the `jsonb` value on the left of the operator according to the rule that the operator implements, parameterized by the SQL value on the right of the operator.

#### Remove key-value pair(s) from an _object_

To remove a single key-value pair:

```postgresql
do $body$
declare
  j_left constant jsonb := '{"a": "x", "b": "y"}';
  key constant text := 'a';
  j_expected constant jsonb := '{"b": "y"}';
begin
  assert
    j_left - key = j_expected,
 'assert failed';
end;
$body$;
```

To remove several key-value pairs:

```postgresql
do $body$
declare
  j_left constant jsonb := '{"a": "p", "b": "q", "c": "r"}';
  key_list constant text[] := array['a', 'c'];
  j_expected constant jsonb := '{"b": "q"}';
begin
  assert
    j_left - key_list = j_expected,
 'assert failed';
end;
$body$;
```

#### Remove single value from an _array_

Thus:

```postgresql
do $body$
declare
  j_left constant jsonb := '[1, 2, 3, 4]';
  idx constant int := 0;
  j_expected constant jsonb := '[2, 3, 4]';
begin
  assert
    j_left - idx = j_expected,
 'assert failed';
end;
$body$;
```

There seems not to be a way to remove several values from an _array_ at a list of indexes analogous to the ability to remove several key-value pairs from an _object_ with a list of pair keys. The obvious attempt fails with this error:

```
operator does not exist: jsonb - integer[]
```

Obviously, you can achieve the result at the cost of verbosity thus:

```postgresql
do $body$
declare
  j_left constant jsonb := '[1, 2, 3, 4, 5, 7]';
  idx constant int := 0;
  j_expected constant jsonb := '[4, 5, 7]';
begin
  assert
    ((j_left - idx) - idx) - idx = j_expected,
 'assert failed';
end;
$body$;
```

### Remove a single key-value pair from an _object_ or a single value from an _array_ at the specified path: `#-`

Thus:

```postgresql
do $body$
declare
  j_left constant jsonb := '["a", {"b":17, "c": ["dog", "cat"]}]';
  path constant text[] := array['1', 'c', '0'];
  j_expected constant jsonb := '["a", {"b": 17, "c": ["cat"]}]';
begin
  assert
    j_left #- path = j_expected,
 'assert failed';
end;
$body$;
```

Just as with the `#>` and `#>>` operators, array index values are presented as convertible `text` values. Notice that the address of each JSON array element along the path is specified JSON-style, where the index starts at zero.

## Built-in functions for displaying _json_ and _jsonb_ values and for observing derived properties

### _jsonb_pretty()_

This function formats the text representation of the JSON value that the input `jsonb` actual argument represents, using whitespace to make it maximally easily human readable.

Here is its signature:

```
input value        jsonb
return value       text
```

Because, in the main, JSON is mechanically generated and mechanically consumed, it's unlikely that you'll use `jsonb_pretty()` in final production code. However, because JSON isn't self-describing, and because external documentation of a corpus's data-representation intent isn't always available, developers often need to study a representative set of extant JSON values and deduce the intended rules of composition. This task is made hugely easier when JSON values are formatted in a standard, well designed, way.

Notice that `jsonb_pretty()` has no variant for a plain `json` value. However, you can trivially typecast a `json` value to a `jsonb` value.

The pretty format makes conventional, and liberal, use of newlines and spaces, thus:

```postgresql
do $body$
declare
  orig_text constant text := '
    {
      "a": 1,
      "b": {"x": 1, "y": 19},
      "c": true
    }';

  expected_pretty_text text :=
'{
    "a": 1,
    "b": {
        "x": 1,
        "y": 19
    },
    "c": true
}';

  j constant jsonb := orig_text::jsonb;
  pretty_text constant text := jsonb_pretty(j);
begin
  assert
    pretty_text = expected_pretty_text,
 'assert failed';
end;
$body$;
```

You might prefer simply to use the `::text` typecast to visualize a small `jsonb` value.

### _jsonb_array_length()_ and _json_array_length()_

The functions in this pair require that the supplied JSON value is an _array_. Here is the signature for the `jsonb` variant:

```
input value        jsonb
return value       integer
```

The functions return the count of values (primitive or compound) in the array. You can use this to iterate over the elements of a JSON _array_ using the  `->` operator.

```postgresql
do $body$
declare
  j constant jsonb := '["a", 42, true, null]';
  last_idx constant int := (jsonb_array_length(j) - 1);

  expected_typeof constant text[] :=
    array['string', 'number', 'boolean', 'null'];

  n int := 0;
begin
  for n in 1..last_idx loop
    assert
      jsonb_typeof(j -> n) = expected_typeof[n + 1],
    'unexpected';
  end loop;
end;
$body$;
```

This example uses the `jsonb_typeof()` function.

Reading the values themselves would need to use a `case` statement that tests the emergent JSON data type and that selects the leg whose assignment target has the right SQL data type. This is straightforward only for primitive JSON values. If a compound JSON value is encountered, then it must be decomposed, recursively, until the ultimate JSON primitive value leaves are reached.

This complexity reflects the underlying impedance mismatch between JSON's type system and SQL's type system. Introspecting a JSON value when you have no _a priori_ understanding of its structure is tricky.

### _jsonb_typeof()_ and _json_typeof()_

These functions return the type of the JSON value as a SQL `text` value. Here is the signature for the `jsonb` variant:

```
input value        jsonb
return value       text
```

Possible types are _string_, _number_, _boolean_, _null_,  _object_, and _array_ — as follows.

```postgresql
do $body$
declare
  j_string   constant jsonb := '"dog"';
  j_number   constant jsonb := '42';
  j_boolean  constant jsonb := 'true';
  j_null     constant jsonb := 'null';
  j_object   constant jsonb := '{"a": 17, "b": "x", "c": true}';
  j_array    constant jsonb := '[17, "x", true]';
begin
  assert
    jsonb_typeof(j_string)   = 'string'  and
    jsonb_typeof(j_number)   = 'number'  and
    jsonb_typeof(j_boolean)  = 'boolean' and
    jsonb_typeof(j_null)     = 'null'    and
    jsonb_typeof(j_object)   = 'object'  and
    jsonb_typeof(j_array)    = 'array',
 'assert failed';
end;
$body$;
```

## Built-in functions for creating _json_ and _jsonb_ values from scratch

### _to_jsonb()_ and _to_json()_

Each takes a single value of any single SQL primitive or compound data type that allows a JSON representation. Here is the signature for the `jsonb` variant:

```
input value        anyelement
return value       jsonb
```

Use this _ysqlsh_ script to create types `t1` and `t2` and then to execute the `do` block that asserts that the behavior is as expected. For an arbitrary nest of SQL `record` and SQL `array` values, readability is improved by building the compound value from the bottom up.

```postgresql
create type t1 as(a int, b text);
create type t2 as(x text, y boolean, z t1[]);

do $body$
declare
  j1_dog  constant jsonb := to_jsonb('In the'||Chr(10)||'"dog house"'::text);
  j2_dog  constant jsonb := '"In the\n\"dog house\""';

  j1_42   constant jsonb := to_jsonb(42::numeric);
  j2_42   constant jsonb := '42';

  j1_true   constant jsonb := to_jsonb(true::boolean);
  j2_true   constant jsonb := 'true';

  j1_null   constant jsonb := to_jsonb(null::boolean);
  j2_null   constant jsonb := 'null';

  j1_array constant jsonb := to_jsonb(array['a', 42, true]::text[]);
  j2_array  constant jsonb := '["a", "42", "true"]';

  v1 t1   := (17::int, 'dog'::text);
  v2 t1   := (42::int, 'cat'::text);
  v3 t1[] := array[v1, v2];
  v4 t2   := ('frog', true, v3);
  j1_object constant jsonb := to_jsonb(v4::t2);
  j2_object constant jsonb :=
    '{"x": "frog",
      "y": true,
      "z": [{"a": 17, "b": "dog"}, {"a": 42, "b": "cat"}]}';
begin
  assert
    j1_dog   = j2_dog  and
    j1_42    = j2_42   and
    j1_true  = j2_true and
    j1_array = j2_array,
 'assert failed';
end;
$body$;
```

### _array_to_json()_

This has one variant that returns a `json` value. Here is the signature: 

```
input value        anyarray
pretty             boolean
return value       json
```

The first (mandatory) formal parameter is any SQL `array` whose elements might be compound values. The second formal parameter is optional. When it is _true_, line feeds are added between dimension-1 elements.

```postgresql
do $body$
declare
  sql_array constant text[] := array['a', 'b', 'c'];

  j_false constant json := array_to_json(sql_array, false);
  j_true  constant json := array_to_json(sql_array, true);

  expected_j_false constant json := '["a","b","c"]';
  expected_j_true  constant json := 
'["a",
 "b",
 "c"]';
begin
  assert
    (j_false::text = expected_j_false::text) and
    (j_true::text  = expected_j_true::text),
  'unexpected';
end;
$body$;
```

The `array_to_json()` function has no practical advantage over `to_json()` or `to_jsonb()` and is restricted because it explicitly handles a SQL `array` and cannot handle a SQL `record` (at top level). Moreover, it can produce only a JSON _array_ whose values all have the same data type. If you want to pretty-print the text representation of the result JSON value, you can use the `::text` typecast or `jsonb_pretty()`.

### _row_to_json()_

This has one variant that returns a `json` value. Here is the signature: 

```
input value        record
pretty             boolean
return value       json
```

The first (mandatory) formal parameter is any SQL `record` whose fields might be compound values. The second formal parameter is optional. When it is _true_, line feeds are added between fields. Use this _ysqlsh_ script to create the required type `t` and then to execute the `assert`.

```postgresql
create type t as (a int, b text);

do $body$
declare
  row constant t := (42, 'dog');
  j_false constant json := row_to_json(row, false);
  j_true  constant json := row_to_json(row, true);
  expected_j_false constant json := '{"a":42,"b":"dog"}';
  expected_j_true  constant json := 
'{"a":42,
 "b":"dog"}';
begin
  assert
    (j_false::text = expected_j_false::text) and
    (j_true::text  = expected_j_true::text),
  'unexpected';
end;
$body$;
```

The `row_to_json()` function has no practical advantage over `to_json()` or `to_jsonb()` and is restricted because it explicitly handles a SQL `record` and cannot handle a SQL `array` (at top level). If you want to pretty-print the text representation of the JSON value result, you can use the `::text` typecast or `jsonb_pretty()`.

### _jsonb_build_array()_ and _json_build_array()_

Here is the signature for the `jsonb` variant:

```
input value        VARIADIC "any"
return value       jsonb
```

These two functions take an arbitrary number of actual arguments of mixed SQL data types. (The PostgreSQL documentation uses the term _"[variadic](https://en.wikipedia.org/wiki/Variadic_function)"_ to characterize this.) The data type of each argument must have a direct JSON equivalent or allow implicit conversion to such an equivalent. Use this _ysqlsh_ script to create the required type `t` and then to execute the `assert`.

```postgresql
create type t as (a int, b text);

do $body$
declare
  v_17 constant int := 17;
  v_dog constant text := 'dog';
  v_true constant boolean := true;
  v_t constant t := (17::int, 'cat'::text);

  j constant jsonb := jsonb_build_array(v_17, v_dog, v_true, v_t);
  expected_j constant jsonb := '[17, "dog", true, {"a": 17, "b": "cat"}]';
begin
  assert
    j = expected_j,
  'unexpected';
end;
$body$;
```

Using `jsonb_build_array()` is straightforward when you know the structure of your target JSON value statically, and just discover the values dynamically. However, it doesn't accommodate the case that you discover the desired structure dynamically.

The following _ysqlsh_ script shows a feasible general workaround for this use case. The helper function `f()` generates the variadic argument list as the text representation of a comma-separated list of manifest constants of various data types. Then it invokes `jsonb_build_array()` dynamically. Obviously this brings a performance cost. But you might not have an alternative.

```postgresql
create function f(variadic_array_elements in text) returns jsonb
  immutable
  language plpgsql
as $body$
declare
  stmt text := '
    select jsonb_build_array('||variadic_array_elements||')';
  j jsonb;
begin
  execute stmt into j;
  return j;
end;
$body$;

do $body$
declare
  v_17 constant int := 17;
  v_dog constant text := 'dog';
  v_true constant boolean := true;
  v_t constant t := (17::int, 'cat'::text);

  j constant jsonb := jsonb_build_array(v_17, v_dog, v_true, v_t);
  expected_j constant jsonb := f(
    $$
      17::integer,
      'dog'::text,
      true::boolean,
      (17::int, 'cat'::text)::t
    $$
    );
begin
  assert
    j = expected_j,
  'unexpected';
end;
$body$;
```

### _jsonb_build_object()_ and _json_build_object()_

Here is the signature for the `jsonb` variant:

```
input value        VARIADIC "any"
return value       jsonb
```

The `jsonb_build_object()` variadic function is the obvious counterpart to `jsonb_build_array()`. The argument list has the form:

```
key1::text, value1::the_data type1,
key1::text, value2::the_data type2,
...
keyN::text, valueN::the_data typeN
```

 Use this _ysqlsh_ script to create the required type `t` and then to execute the `assert`.

```postgresql
create type t as (a int, b text);

do $body$
declare
  v_17 constant int := 17;
  v_dog constant text := 'dog';
  v_true constant boolean := true;
  v_t constant t := (17::int, 'cat'::text);

  j constant jsonb := jsonb_build_object(
    'a', v_17,
    'b', v_dog,
    'c', v_true,
    'd', v_t);
  expected_j constant jsonb := 
    '{"a": 17, "b": "dog", "c": true, "d": {"a": 17, "b": "cat"}}';
begin
  assert
    j = expected_j,
  'unexpected';
end;
$body$;
```

Just as with `jsonb_build_array())`, using `jsonb_build_object()` is straightforward when you know the structure of your target JSON value statically, and just discover the values dynamically. But again, it doesn't accommodate the case that you discover the desired structure dynamically.

The following _ysqlsh_ script shows a feasible general workaround for this use case. The helper function `f()` generates the variadic argument list as the text representation of a comma-separated list of manifest constants of various data types. Then it invokes `jsonb_build_object()` dynamically. Obviously this brings a performance cost. But you might not have an alternative.

```postgresql
create function f(variadic_array_elements in text) returns jsonb
  immutable
  language plpgsql
as $body$
declare
  stmt text := '
    select jsonb_build_object('||variadic_array_elements||')';
  j jsonb;
begin
  execute stmt into j;
  return j;
end;
$body$;

do $body$
declare
  v_17 constant int := 17;
  v_dog constant text := 'dog';
  v_true constant boolean := true;
  v_t constant t := (17::int, 'cat'::text);

  j constant jsonb :=  f(
    $$
      'a'::text, 17::integer,
      'b'::text, 'dog'::text,
      'c'::text, true::boolean,
      'd'::text, (17::int, 'cat'::text)::t
    $$);
  expected_j constant jsonb := 
    '{"a": 17, "b": "dog", "c": true, "d": {"a": 17, "b": "cat"}}';
begin
  assert
    j = expected_j,
  'unexpected';
end;
$body$;
```

### _jsonb_object()_ and _json_object()_

Here is the signature for the `jsonb` variant:

```
input value        text[]  OR text[][]  OR  text[], text[]
return value       jsonb
```

The `jsonb_object()` function achieves a similar effect to `jsonb_build_object()` but with significantly less verbose syntax.

Precisely because you present a single `text` actual, you can avoid the fuss of dynamic invocation and of dealing with interior single quotes that this brings in its train.

However, it has the limitation that the primitive values in the resulting JSON value can only be _string_. It has three overloads.

The first overload has a single `text[]` formal whose actual text expresses the variadic intention conventionally: the alternating _comma_ separated items are the respectively the key and the value of a key-value pair.

```postgresql
do $body$
declare
  array_values constant text[] :=
    array['a', '17', 'b', $$'Hello', you$$, 'c', 'true'];

  j constant jsonb :=  jsonb_object(array_values);
  expected_j constant jsonb := 
    $${"a": "17", "b": "'Hello', you", "c": "true"}$$;
begin
  assert
    j = expected_j,
  'unexpected';
end;
$body$;
```

Compare this result with the result from supplying the same primitive SQL values to the `jsonb_build_object()` function. There, the data types of the SQL values are properly honored: The _numeric_ `17` and the _boolean_ `true` are represented by the proper JSON primitive types. But with `jsonb_object()` there is simply no way to express that `17` should be taken as a JSON _number_ value and `true` should be taken as a JSON _boolean_ value.

The potential loss of data type fidelity brought by `jsonb_object()` seems to be a high price to pay for the reduction in verbosity. On the other hand, `jsonb_object()` has the distinct advantage over `jsonb_build_object()` that you don't need to know statically how many key-value pairs the target JSON _object_ is to have.

If you think that it improves the clarity, you can use the second overload. This has a single `text[][]` formal—in other words an array of arrays.

```postgresql
do $body$
declare
  array_values constant text[][] :=
    array[
      array['a',  '17'],
      array['b',  $$'Hello', you$$],
      array['c',  'true']
    ];

  j constant jsonb :=  jsonb_object(array_values);
  expected_j constant jsonb := 
    $${"a": "17", "b": "'Hello', you", "c": "true"}$$;
begin
  assert
    j = expected_j,
  'unexpected';
end;
$body$;
```

This produces the identical result to that produced by the example for the first overload.

Again, if you think that it improves the clarity, you can use the third overload. This has a two `text[]` formals. The first expresses the list keys of the key-values pairs. And the second expresses the list values of the key-values pairs. The items must correspond pairwise, and clearly each array must have the same number of items. For example:

```
do $body$
declare
  array_keys constant text[] :=
    array['a',  'b',              'c'   ];
  array_values constant text[] :=
    array['17', $$'Hello', you$$, 'true'];

  j constant jsonb :=  jsonb_object(array_keys, array_values);
  expected_j constant jsonb := 
    $${"a": "17", "b": "'Hello', you", "c": "true"}$$;
begin
  assert
    j = expected_j,
  'unexpected';
end;
$body$;
```

This, too, produces the identical result to that produced by the example for the first overload.

## Builtin functions for miscellaneous processing of _json_ and _jsonb_ values

### _jsonb_each()_ and _json_each()_

Here is the signature for the `jsonb` variant:

```
input value        jsonb
return value       SETOF (text, jsonb)
```

The functions in this pair require that the supplied JSON value is an _object_. They return a row set with columns _"key"_ (as a SQL `text`) and _"value"_ (as a SQL `jsonb`). Use this _ysqlsh_ script to create the required type `t` and then to execute the `assert`.

```postgresql
create type t as (k text, v jsonb);

do $body$
declare
  object constant jsonb :=
    '{"a": 17, "b": "dog", "c": true, "d": {"a": 17, "b": "cat"}}';

  k_v_pairs t[] := null;
  expected_k_v_pairs constant t[] := 
    array[
      ('a', '17'::jsonb),
      ('b', '"dog"'::jsonb),
      ('c', 'true'::jsonb),
      ('d', '{"a": 17, "b": "cat"}'::jsonb)
    ];

  k_v_pair t;
  n int := 0;
begin
  for k_v_pair in (
    select key, value from jsonb_each(object)
    )
  loop
    n := n + 1;
    k_v_pairs[n] := k_v_pair;
  end loop;

  assert
    k_v_pairs = expected_k_v_pairs,
  'unexpected';
end;
$body$;
```


### _jsonb_extract_path()_ and _json_extract_path()_

Here is the signature for the `jsonb` variant:

```
input value        jsonb, VARIADIC text
return value       jsonb
```

These are functionally identical to the `#>` operator. The invocation of `#>` can be mechanically transformed to use `jsonb_extract_path()` by these steps:

- Add the function invocation with its required parentheses.
- Replace `#>` with a comma.
- Write the path as a comma-separated list of terms, where both integer array indexes and key-value keys are presented as `text` values, taking advantage of the fact that the function is variadic).

This `DO` block shows the invocation of `#>` and `jsonb_extract_path()` vertically to make visual comparison easy.

```
do $body$
declare
  j constant jsonb :=
    '[1, {"x": [1, true, {"a": "cat", "b": "dog"}, 3.14159], "y": true}, 42]';

  jsub_1 constant jsonb :=                    j #> array['1', 'x', '2', 'b'];
  jsub_2 constant jsonb := jsonb_extract_path(j,         '1', 'x', '2', 'b');

  expected_jsub constant jsonb := '"dog"';
begin
  assert
    (jsub_1 = expected_jsub) and
    (jsub_2 = expected_jsub),
  'unexpected';
end;
$body$;
```

Strangely, even though `jsonb_extract_path()`is variadic, each step that defines the path must be presented as a convertible SQL `text`, even when its meaning is a properly expressed by a SQL `integer`.

The function form is more verbose than the operator form. Moreover, the fact that the function is variadic makes it impossible to invoke it statically (in PL/pgSQL code) when the path length isn't known until run time. There seems, therefore, to be no reason to prefer the function form to the operator form.

### _jsonb_extract_path_text()_ and _json_extract_path_text()_

Here is the signature for the `jsonb` variant:

```
input value        jsonb, VARIADIC text
return value       text
```

The result `jsonb_extract_path_text()` bears the same relationship to the result of its `jsonb_extract_path()`as does the result of the `#>>` operator to that of the `#>` operator. For the same reasons that there is no reason to prefer`jsonb_extract_path()` over `#>`, there is no reason to prefer `jsonb_extract_path_text()` over `#>>`.

### _jsonb_object_keys()_ and _json_object_keys()_

Here is the signature for the `jsonb` variant:

```
input value        jsonb
return value       SETOF text
```

The functions in this pair require that the supplied JSON value is an _object_. They transform the list of key names into a set (i.e. table) of `text` values. Notice that the returned keys are ordered alphabetically.

```postgresql
do $body$
declare
  object constant jsonb :=
    '{"b": 1, "c": true, "a": {"p":1, "q": 2}}';

  keys text[] := null;

  expected_keys constant text[] :=
    array['a', 'b', 'c'];

  k text;
  n int := 0;
begin
  for k in (
    select * from jsonb_object_keys(object)
   )
  loop
    n := n + 1;
    keys[n] := k;
  end loop;

  assert
    keys = expected_keys,
  'unexpected';
end;
$body$;
```

### _jsonb_populate_record_ and _json_populate_record_

Here is the signature for the `jsonb` variant:

```
input value        anyelement, jsonb
return value       anyelement
```

The functions in this pair require that the supplied JSON value is an _object_. They translate the JSON _object_ into the equivalent SQL` record`. The data type of the `record` must be defined as a schema-level `type` whose name is passed via the function's first formal parameter using the locution `null:type_identifier`. The JSON value is passed via the second formal parameter. Use this _ysqlsh_ script to create the required types `t1` and `t2`, and then to execute the `assert`.

```
create type t1 as ( d int, e text);
create type t2 as (a int, b text[], c t1);

do $body$
declare
  nested_object constant jsonb :=
    '{"a": 42, "b": ["cat", "dog"], "c": {"d": 17, "e": "frog"}}';

  result constant t2 := jsonb_populate_record(null::t2, nested_object);

  expected_a constant int := 42;
  expected_b constant text[] := array['cat', 'dog'];
  expected_c constant t1 := (17, 'frog');
  expected_result constant t2 := (expected_a, expected_b, expected_c);
begin
  assert
    result = expected_result,
  'unexpected';
end;
$body$;
```

### _jsonb_to_record()_ and _json_to_record()_

Here is the signature for the `jsonb` variant:

```
input value        jsonb
return value       record
```

The `jsonb_to_record()` function is a syntax variant of the same functionality that  `jsonb_populate_record()` provides. It doesn't need a schema-level type but, rather, uses the special SQL locution `select... as on_the_fly(<record definition>)`. Notice that `on_the_fly` is a nonce name, made up for this example. Anything will suffice. Use this _ysqlsh_ script to create the  type `t` that just `jsonb_populate_record()` requires, to convert the input `jsonb` into a SQL `record` using each of  `jsonb_populate_record()` and `jsonb_to_record`, and then to execute the `assert`.

```postgresql
create type t as (a int, b text);

do $body$
declare
  object constant jsonb :=
    '{"a": 42, "b": "dog"}';

  result_1 constant t := jsonb_populate_record(null::t, object);
  result_2 t;
  expected_result constant t := (42, 'dog');
begin
  select a, b
  into strict result_2
  from jsonb_to_record(object)
  as on_the_fly(a int, b text);

  assert
    (result_1 = expected_result) and 
    (result_2 = expected_result),
  'unexpected';
end;
$body$;
```

The nominal advantage of `jsonb_to_record()`, that it doesn't need a schema-level type, is lost when the input JSON _object_ has another _object_ as the value of one of its keys. Consider this _ysqlsh_ script:
```
create type t1 as ( d int, e text);
create type t2 as (a int, b text[], c t1);

do $body$
declare
  nested_object constant jsonb :=
    '{"a": 42, "b": ["cat", "dog"], "c": {"d": 17, "e": "frog"}}';

  result_1 constant t2 := jsonb_populate_record(null::t2, nested_object);
  result_2 t2;

  expected_a constant int := 42;
  expected_b constant text[] := array['cat', 'dog'];
  expected_c constant t1 := (17, 'frog');
  expected_result constant t2 := (expected_a, expected_b, expected_c);
begin
  select a, b, c
  into strict result_2
  from jsonb_to_record(nested_object)
  as on_the_fly(a int, b text[], c t1);

  assert
    (result_1 = expected_result) and 
    (result_2 = expected_result),
  'unexpected';
end;
$body$;
```
It does show that `jsonb_populate_record()` and `jsonb_to_record()` both produce the same result from the same input. But, here, the `on_the_fly` type definition in the `as` clause

So the outer type `t2` can be defined on the fly in the `as` clause but it references the inner schema-level type `t1`. It isn't possible to absorb `t1`'s definition into the `as` clause. Moreover, the fact that `jsonb_to_record()` cannot be used in an ordinary assignment but requires a SQL `select ... into` statement is a serious drawback.

It would seem, therefore, that the `jsonb_to_record()` variant has no practical advantage over the `jsonb_populate_record()` variant.

### _jsonb_populate_recordset()_ and _json_populate_recordset()_

Here is the signature for the `jsonb` variant:

```
input value        anyelement, jsonb
return value       SETOF anyelement
```

The functions in this pair and are a natural extension of the functionality of `jsonb_populate_record()`.

Each requires that the supplied JSON value is an _array_, each of whose values is an _object_ which is compatible with the specified SQL `record` which is defined as a `type` whose name is passed via the function's first formal parameter using the locution `null:type_identifier`. The JSON value is passed via the second formal parameter. The result is a set (i.e. a table) of `record`s of the specified type.

Use this _ysqlsh_ script to create the  type `t`, and then to execute the `assert`. Notice the because the result is a table, it must be materialized in a `cursor for loop`. Each selected row is accumulated in an array of type `t[]`. The expected result is also established in an array of type `t[]`. The input JSON _array_ has been contrived, by sometimes not having a key `"a"` or a key `"b"` so that the resulting records sometimes have `null` fields. The equility test in the `assert` has to accommodate this. Therefore a helper PL/pgSQL function, `same_as()` is created to encapsulate the logic.

```
create type t as (a int, b int);

create function same_as(a1 t[], a2 t[]) returns boolean
  immutable
  language plpgsql
as $body$
declare
  v1 t;
  v2 t;
  n int := 0;
  result boolean := true;
begin
  foreach v1 in array a1 loop
    n := n + 1;
    v2 := a2[n];

    result := result and
      ((v1.a = v2.a) or (v1.a is null and v2.a is null))
      and
      ((v1.b = v2.b) or (v1.b is null and v2.b is null));
  end loop;
  return result;
end;
$body$;

do $body$
declare
  array_of_objects constant jsonb :=
    '[
      {"a": 1, "b": 2},
      {"b": 4, "a": 3},
      {"a": 5, "c": 6},
      {"b": 7, "d": 8},
      {"c": 9, "d": 0}
    ]';

  rows t[];
  expected_rows constant t[] :=
    array[
      (   1,    2)::t,
      (   3,    4)::t,
      (   5, null)::t,
      (null,    7)::t,
      (null, null)::t
    ];
  row t;
  n int := 0;
begin
  for row in (
    select a, b
    from jsonb_populate_recordset(null::t, array_of_objects)
    )
  loop
    n := n + 1;
    rows[n] := row;
  end loop;

  assert
    same_as(rows, expected_rows),
  'unexpected';
end;
$body$;
```
### _jsonb_to_recordset()_ and _json_to_recordset()_

Here is the signature for the `jsonb` variant:

```
input value        jsonb
return value       SETOF record
```

The function `jsonb_to_recordset()` bears the same relationship to `jsonb_to_record()` as  `json_populate_recordset()` bears to `json_populate_record()`. Therefore the `DO` block that demonstrated `json_populate_recordset()`'s functionality can be easily extended to demonstrate `jsonb_to_recordset()`'s functionality as well and to show that their results are identical. The `DO` block needs the same type `t` and function `same_as()` that the _ysqlsh_ script for `json_populate_recordset()`defined.

```
do $body$
declare
  array_of_objects constant jsonb :=
    '[
      {"a": 1, "b": 2},
      {"b": 4, "a": 3},
      {"a": 5, "c": 6},
      {"b": 7, "d": 8},
      {"c": 9, "d": 0}
    ]';

  rows_1 t[];
  rows_2 t[];
  expected_rows constant t[] :=
    array[
      (   1,    2)::t,
      (   3,    4)::t,
      (   5, null)::t,
      (null,    7)::t,
      (null, null)::t
    ];
  row t;
  n int := 0;
begin
  for row in (
    select a, b
    from jsonb_populate_recordset(null::t, array_of_objects)
    )
  loop
    n := n + 1;
    rows_1[n] := row;
  end loop;

  for row in (
    select a, b
    from jsonb_to_recordset(array_of_objects)
    as on_the_fly(a int, b int)
    )
  loop
    n := n + 1;
    rows_2[n] := row;
  end loop;

  assert
    same_as(rows_1, expected_rows) and
    same_as(rows_2, expected_rows),
  'unexpected';
end;
$body$;
```
The same considerations apply if the target `record` data type has a non-primitive field. It would seem, therefore, that the `jsonb_to_recordset()` variant has no practical advantage over the `jsonb_populate_recordset()` variant.

### _jsonb_array_elements()_ and _json_array_elements()_

Here is the signature for the `jsonb` variant:

```
input value        jsonb
return value       SETOF jsonb
```

The functions in this pair require that the supplied JSON value is an _array_, and transform the list into a table. They are the counterparts, for an _array_ of primitive JSON values, to `jsonb_populate_recordset()` for JSON _objects_.

```
do $body$
declare
  j_array constant jsonb := '["cat", "dog house", 42, true, null, {"x": 17}]';
  j jsonb;

  elements jsonb[];
  expected_elements constant jsonb[] :=
    array[
      '"cat"'::jsonb,
      '"dog house"'::jsonb,
      '42'::jsonb,
      'true'::jsonb,
      'null'::jsonb,
      '{"x": 17}'::jsonb
    ];

  n int := 0;
begin
  for j in (select * from jsonb_array_elements(j_array)) loop
    n := n + 1;
    elements[n] := j;
  end loop;

  assert
    elements = expected_elements,
  'unexpected';
end;
$body$;
```

### _jsonb_array_elements_text()_ and _json_array_elements_text()_

Here is the signature for the `jsonb` variant:

```
input value        jsonb
return value       SETOF text
```

The function `jsonb_array_elements_text()` bears the same relationship to `jsonb_array_elements()` that the other `*text()` functions bear to their plain counterparts: it's the same relationship that the `->>` and `#>>` operators bear, respectively to `->` and `#>`. This example uses the same JSON _array_ input that was used to illustrate `jsonb_array_elements()` .

```
do $body$
declare
  j_array constant jsonb := '["cat", "dog house", 42, true, {"x": 17}, null]';
  t text;

  elements text[];
  expected_elements constant text[] :=
    array[
      'cat',
      'dog house',
      '42',
      'true',
      '{"x": 17}',
      null
    ];

  n int := 0;
begin
  for t in (select * from jsonb_array_elements_text(j_array)) loop
    n := n + 1;
    elements[n] := t;
  end loop;

  for n in 1..5 loop
    assert
      elements[n] = expected_elements[n],
    'unexpected';
  end loop;

  assert
    elements[6] is null and expected_elements[6] is null,
  'unexpected';
end;
$body$;
```

This highlights the fact that the resulting values are equivalent SQL primitive values, cast to `text` values, rather than the RFC 7159 representations of the actual JSON values. In particular, `42` is the two characters `4` and `2` and `true` is the four characters `t` , `r`, `u`, and `e`.  Moreover, the sixth row has a genuine `null`. This is why the `assert` needs to be programmed more verbosely than for the `jsonb_array_elements()` example.

This example emphasizes the impedance mismatch between a JSON _array_ and a SQL `array`: the former allows values of heterogeneous data types; but the latter allows only values of the same data type—as was used to declare the array.

If you have prior knowledge of the convention to which the input JSON document adheres, you can cast the output of `jsonb_array_elements_text()` to, say, `integer` or `boolean`. For example, this:
```
create domain length as numeric
  not null
  check (value > 0);

select value::length
from jsonb_array_elements_text(
  '[17, 19, 42, 47, 53]'::jsonb
  );
```

generates a table of genuine `integer` values that honor the constraints that the `domain` defines. If you make one of the input elements negative, then you get this error:

```
value for domain length violates check constraint "length_check"
```

And if you make of the input elements the JSON `null`, then you get this error:

```
domain length does not allow null values
```

Here's the same idea for `boolean` values:

```
create domain truth as boolean
  not null;

select value::truth
from jsonb_array_elements_text(
  '[true, false, true, false, false]'::jsonb
  );
```

It produces this output in _ysqlsh_:
```
 value 
-------
 t
 f
 t
 f
 f
```

Notice that `t` and `f` are the _ysqlsh_ convention for displaying `boolean` values.

### _jsonb_strip_nulls()_ and _json_strip_nulls()_

Here is the signature for the `jsonb` variant:

```
input value        jsonb
return value       jsonb
```

Each function returns a value of the same data type as that of the actual with which it is invoked. They find all key-value pairs at any depth in the hierarchy of the supplied JSON compound value (such a pair can occur only as an element of an _object_) and return a JSON value where each pair whose value is _null_. has been removed. By definition, they leave _null_ values within _arrays_ untouched.

```
do $body$
declare
  j constant jsonb :=
    '{
      "a": 17,
      "b": null,
      "c": {"x": null, "y": "dog"},
      "d": [42, null, "cat"]
    }';

  stripped_j constant jsonb := jsonb_strip_nulls(j);

  expected_stripped_j constant jsonb :=
    '{
      "a": 17,
      "c": {"y": "dog"},
      "d": [42, null, "cat"]
    }';
begin
  assert
    stripped_j = expected_stripped_j,
  'unexpected';
end;
$body$;
```
### _jsonb_set()_ and _jsonb_insert()_

These two functions require a `jsonb` input. There are no variants for plain `json`.

- Use `jsonb_set()` to change an existing JSON value, i.e. the value of an existing key-value pair in a JSON _object_ or the value at an existing index in a JSON array.

- Use `jsonb_insert()` to insert a new value, either as the value for a key that doesn't yet exist in a JSON _object_ or beyond the end or before the start of the index range for a JSON _array_.

It turns out that the effect of the two functions is the same in some cases. This brings useful "upsert" functionality when the target is a JSON _array_. Here are their signatures:

```
jsonb_set()
-----------
  jsonb_in           jsonb
  path               text[]
  replacement        jsonb
  create_if_missing  boolean default true
  return value       jsonb
```
and:
```
jsonb_insert()
--------------
  jsonb_in           jsonb
  path               text[]
  replacement        jsonb
  insert_after       boolean default false
  return value       jsonb
```


The meaning of the defaulted boolean formal parameter is context dependent.

The input JSON value must be either an _object_ or an _array_ — in other words, it must have elements that can be addressed by a path.

#### Semantics when _json_in_ is an _object_

An _object_ is a set of key-value pairs where each key is unique and the order is undefined and insignificant. (As explained earlier, when a JSON manifest constant is parsed, or when two JSON values are concatenated, and if a key is repeated, then the last-mentioned in left-to-right order wins.) The functionality is sufficiently illustrated by a `json_in` value that has just primitive values. The result of each function invocation is the same.

```
do $body$
declare
  j constant jsonb := '{"a": 1, "b": 2, "c": 3}';
  path constant text[] := array['d'];
  new_number constant jsonb := '4';

  j_set constant jsonb := jsonb_set(
    jsonb_in          => j,
    path              => path,
    replacement       => new_number,
    create_if_missing => true);

  j_insert constant jsonb := jsonb_insert(
    jsonb_in          => j,
    path              => path,
    replacement       => new_number,
    insert_after      => false);

  expected_j constant jsonb := '{"a": 1, "b": 2, "c": 3, "d": 4}';

begin
  assert
    j_set = expected_j and
    j_insert = expected_j,
  'unexpected';
end;
$body$;
```

Notice that the specified `path`, the key `"d"` doesn't yet exist. Each function call asks to produce the result that the key `"d"` should exist with  the value `4`. So, as we see, the effect of each, as written above, is the same.

If `jsonb_set()` is invoked with `create_if_missing=>false`, then its result is the same as the input. But if `jsonb_insert()` is invoked with `insert_after=>true`, then its output is the same as when it's invoked with `insert_after=>false`. This reflects the fact that the order of key-value pairs in an _object_ is insignificant.

What if `path` specifies a key that does already exist? Now `jsonb_insert()` causes this error when it's invoked both with `insert_after=>true` and with `insert_after=>false`:
```
cannot replace existing key
Try using the function jsonb_set to replace key value.
```

And this `DO` block quitely succeeds, both when it's invoked with `create_if_missing=>false` and when it's invoked with `create_if_missing=>true`.

```postgresql
do $body$
declare
  j constant jsonb := '{"a": 1, "b": 2, "c": 3}';
  path constant text[] := array['c'];
  new_number constant jsonb := '4';

  j_set constant jsonb := jsonb_set(
    jsonb_in          => j,
    path              => path,
    replacement       => new_number,
    create_if_missing => true);

  expected_j constant jsonb := '{"a": 1, "b": 2, "c": 4}';

begin
  assert
    j_set = expected_j,
  'unexpected';
end;
$body$;
```

#### Semantics when _json_in_ is an _array_

An _array_ is a list of index-addressable values — in other words, the order is undefined and insignificant. Again, the functionality is sufficiently illustrated by a `json_in` value that has just primitive values. Now the result of `jsonb_set()` differs from that of `jsonb_insert()`. 

```postgresql
do $body$
declare
  j constant jsonb := '["a", "b", "c", "d"]';
  path constant text[] := array['3'];
  new_string constant jsonb := '"x"';

  j_set constant jsonb := jsonb_set(
    jsonb_in          => j,
    path              => path,
    replacement       => new_string,
    create_if_missing => true);

  j_insert constant jsonb := jsonb_insert(
    jsonb_in     => j,
    path         => path,
    replacement  => new_string,
    insert_after => true);

  expected_j_set    constant jsonb := '["a", "b", "c", "x"]';
  expected_j_insert constant jsonb := '["a", "b", "c", "d", "x"]';

begin
  assert
    (j_set = expected_j_set) and
    (j_insert = expected_j_insert),
  'unexpected';
end;
$body$;
```

Notice that the path denotes the fourth value and that this already exists.

Here, `jsonb_set()` located the fourth value and set it to `"x"` while `jsonb_insert()` located the fourth value and, as requested by `insert_after=>true`, inserted `"x"` after it. Of course, with `insert_after=>false`, `"x"` is inserted before `"d"`. And (of course, again) the choice for `create_if_missing` has no effect on the result of `jsonb_set()`.

What if the path denotes a value beyond the end of the array?

```
do $body$
declare
  j constant jsonb := '["a", "b", "c", "d"]';
  path constant text[] := array['42'];
  new_string constant jsonb := '"x"';

  j_set constant jsonb := jsonb_set(
    jsonb_in          => j,
    path              => path,
    replacement       => new_string,
    create_if_missing => true);

  j_insert constant jsonb := jsonb_insert(
    jsonb_in          => j,
    path              => path,
    replacement       => new_string,
    insert_after      => true);

  expected_j constant jsonb := '["a", "b", "c", "d", "x"]';

begin
  assert
    j_set = expected_j and
    j_insert = expected_j,
  'unexpected';
end;
$body$;
```

Here, each function had the same effect.

The path, for `jsonb_set()`, is taken to mean the as yet nonexistent fifth value. So, with `create_if_missing=>false`, `jsonb_set()` has no effect.

The path, for `jsonb_insert()`, is also taken to mean the as yet nonexistent fifth value. But now, the choice of `true` or `false` for `insert_after` makes no difference because before, or after, a nonexistent element is simply taken to mean insert it.

Notice that even if the path is specified as `-42` (i.e. an impossible _array_ index) the result is the complementary. So this:

```
do $body$
declare
  j constant jsonb := '["a", "b", "c", "d"]';
  path constant text[] := array['-42'];
  new_string constant jsonb := '"x"';

  j_set constant jsonb := jsonb_set(
    jsonb_in          => j,
    path              => path,
    replacement       => new_string,
    create_if_missing => true);

  j_insert constant jsonb := jsonb_insert(
    jsonb_in          => j,
    path              => path,
    replacement       => new_string,
    insert_after      => true);

  expected_j constant jsonb := '["x", "a", "b", "c", "d"]';

begin
  assert
    j_set = expected_j and
    j_insert = expected_j,
  'unexpected';
end;
$body$;
```

The path, for `jsonb_set()`, is taken to mean a new first value (implying that the existing values all move along one place). So, again, with `create_if_missing=>false`, `jsonb_set()` has no effect. 

The path, for `jsonb_insert()`, is also taken to mean a new first value. So again, the choice of `true` or `false` for `insert_after` makes no difference because before or after, a nonexsistent element is simply taken to mean insert it.
