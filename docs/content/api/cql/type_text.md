---
title: TEXT
summary: String of Unicode characters.
toc: false
---
<style>
table {
  float: left;
}
#psyn {
  text-indent: 50px;
}
#ptodo {
  color: red
}
</style>

## Synopsis
`TEXT` datatype is used to specify data of a string of unicode characters.

## Syntax
```
type_specification ::= { TEXT | VARCHAR }
```

`TEXT` and `VARCHAR` are aliases.

## Semantics
<li>Implicitly, value of type `TEXT` datatype are neither convertible nor comparable to non-text datatypes.</li>
<li>The length of `TEXT` string is virtually unlimited.</li>

## Examples

``` sql
cqlsh:example> CREATE TABLE users(user_name TEXT PRIMARY KEY, full_name VARCHAR);
cqlsh:example> INSERT INTO users(user_name, full_name) VALUES ('jane', 'Jane Doe');
cqlsh:example> INSERT INTO users(user_name, full_name) VALUES ('john', 'John Doe');
cqlsh:example> UPDATE users set full_name = 'Jane Poe' WHERE user_name = 'jane';
cqlsh:example> SELECT * FROM users;
```

```
 user_name | full_name
-----------+-----------
      jane |  Jane Poe
      john |  John Doe
```

## See Also

[Data Types](..#datatypes)
