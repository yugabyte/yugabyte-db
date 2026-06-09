---
title: XML data type and functions [YSQL]
headerTitle: XML data type and functions
linkTitle: XML
summary: XML data type and functions
description: Learn about YSQL support for the XML data type and XML functions.
menu:
  stable_api:
    identifier: api-ysql-datatypes-xml
    parent: api-ysql-datatypes
type: indexpage
showRightNav: true
---

## Synopsis

The XML data type stores Extensible Markup Language (XML) documents. YSQL fully supports the PostgreSQL XML implementation, including:

- XML data type for storing well-formed XML documents
- XML construction functions for building XML fragments
- XPath functions for querying XML documents
- XML predicates for testing document structure
- XMLTABLE construct for extracting tabular data from XML
- XML schema mapping functions for converting between tables and XML

For a comprehensive reference of all XML functions, see the [PostgreSQL XML documentation](https://www.postgresql.org/docs/15/functions-xml.html).

## Data type specification

```ebnf
type_specification ::= xml
```

## Functions and operators

The following key XML functions are supported:

### XML construction

- `xmlcomment(text)` : Constructs an XML comment
- `xmlconcat(xml, ...)` : Concatenates XML fragments
- `xmlelement(name, ...)` : Constructs an XML element
- `xmlattributes(...)` : Specifies attributes for an XML element
- `xmlforest(...)` : Constructs a forest of XML elements
- `xmlpi(name, text)` : Constructs an XML processing instruction
- `xmlroot(xml, ...)` : Modifies the root node of an XML document

### XML parsing and serialization

- `xmlparse(content | document text)` : Parses a text string into XML
- `xmlserialize(content | document xml as type)` : Converts XML to text

### XPath and XML queries

- `xpath(xpath_expr, xml_doc, [namespaces])` : Evaluates XPath expressions
- `xpath_exists(xpath_expr, xml_doc)` : Tests if XPath expression matches
- `xmlexists(xpath_expr PASSING [BY REF] xml_doc)` : Alternative XPath test syntax
- `XMLTABLE(...)` : Converts XML to tabular form

### Aggregation

- `xmlagg(xml [ORDER BY ...])` : Aggregates multiple XML values

### Validation

- `xml_is_well_formed(text)` : Tests if text is well-formed XML
- `xml_is_well_formed_document(text)` : Tests if text is a well-formed XML document
- `xml_is_well_formed_content(text)` : Tests if text is well-formed XML content

### XML predicates

- `IS DOCUMENT` : Tests if XML value is a well-formed document
- `IS NOT DOCUMENT` : Tests if XML value is not a document

### XML schema mapping

Functions to convert between tables and XML are as follows:

- `table_to_xml(table, nulls, tableforest, targetns)`
- `table_to_xmlschema(table, nulls, tableforest, targetns)`
- `table_to_xml_and_xmlschema(table, nulls, tableforest, targetns)`
- `query_to_xml(query, nulls, tableforest, targetns)`
- `query_to_xmlschema(query, nulls, tableforest, targetns)`
- `query_to_xml_and_xmlschema(query, nulls, tableforest, targetns)`
- `cursor_to_xml(cursor, count, nulls, tableforest, targetns)`
- `cursor_to_xmlschema(cursor, nulls, tableforest, targetns)`
- `schema_to_xml(schema, nulls, tableforest, targetns)`
- `schema_to_xmlschema(schema, nulls, tableforest, targetns)`
- `schema_to_xml_and_xmlschema(schema, nulls, tableforest, targetns)`

For the full reference and detailed descriptions of all XML functions, see the [PostgreSQL XML Functions documentation](https://www.postgresql.org/docs/15/functions-xml.html).

## See also

For comprehensive examples and guided tutorials on using XML in YSQL, including:

- Constructing XML documents
- Querying XML with XPath
- Converting between tables and XML
- Handling namespaces

See [XML support in YSQL](../../../../explore/ysql-language-features/xml-ysql/).
