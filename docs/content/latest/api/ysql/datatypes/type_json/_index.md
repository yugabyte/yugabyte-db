---
title: JSON
linktitle: JSON
summary: JSON and JSONB data types
description: JSON data types and functionality
image: /images/section_icons/api/ysql.png
menu:
  latest:
    identifier: api-ysql-datatypes-json
    parent: api-ysql-datatypes
aliases:
  - /latest/api/ysql/datatypes/type_json
isTocNested: true
showAsideToc: true
---

## Synopsis

JSON stands for JavaScript Object Notation, a text format for the serialization of structured data. Its syntax and semantics are defined in [RFC 7159](https://tools.ietf.org/html/rfc7159). JSON is represented by Unicode characters; and such a representation is usually called a _document_. Whitespace outside of _string_ values and _object_ keys (see below) is insignificant.

YSQL supports two data types to represent a JSON document: `json` and `jsonb`. Each rejects any JSON document that does not conform to RFC 7159. The `json` datatype simply stores the text representation of a JSON document as presented. In contrast, the `jsonb` datatype stores a parsed representation of the document hierarchy of subvalues in an appropriate internal format. Some people prefer the mnemonic _"binary"_ for the _"b"_ suffix; others prefer _"better"_. Of course, it takes more computation to store a JSON document as a `jsonb` value than as `json` value. This cost is repaid when subvalues are operated on using the operators and functions that this section describes.

JSON was invented as a data interchange format, initially to allow an arbitrary compound value in a JavaScript program to be serialized, transported as text, and then deserialized in another JavaScript program faithfully to reinstantiate the original compound value. Later, very many other programming languages—including, now, SQL and PL/pgSQL—support serialization to, and deserialization from, JSON. Moreover, it has become common to store JSON as the persistent representation of record in a table with just a primary key column and a `json` or `jsonb` column for facts that could be represented classically in a table design that conforms to the relational model. This pattern arose first in NoSQL databases but it is now widespread in SQL databases.

## Description

```
type_specification ::= { json | jsonb }
```

The remainder of the account of JSON data types and functionality is organized thus:

- [ Primitive and compound JSON data types](../type_json/primitive-and-compound-data-types/)
- [Code example conventions](../type_json/code-example-conventions/)
- [Functions and operators](../type_json/functions-operators/)
- [Create indexes and check constraints on `jsonb` and `json` columns](../type_json/create-indexes-check-constraints/)
