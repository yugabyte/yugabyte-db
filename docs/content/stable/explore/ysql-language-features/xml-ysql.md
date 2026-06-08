---
title: XML support in YSQL
headerTitle: XML support
linkTitle: XML support
description: YSQL XML Support in YugabyteDB.
headcontent: Work with XML data in YSQL
menu:
  stable:
    name: XML support
    identifier: explore-xml-support-ysql
    parent: explore-ysql-language-features
    weight: 610
type: docs
---

The XML data type is for storing Extensible Markup Language (XML) documents. YSQL supports the full range of XML functionality from PostgreSQL, including XML construction functions, XPath queries, and conversion between tables and XML. XML functionality in YSQL is nearly identical to the [XML functionality in PostgreSQL](https://www.postgresql.org/docs/current/datatype-xml.html).

## Overview

The XML data type stores well-formed XML documents. YSQL validates that all XML documents conform to the W3C XML standard and provides functions for:

- Constructing XML documents programmatically
- Querying XML documents using XPath expressions
- Extracting relational data from XML documents using XMLTABLE
- Converting between relational tables and XML

## Setup

{{% explore-setup-single-new %}}

To illustrate XML functionality, create a table to store XML documents about employees:

```sql
CREATE TABLE employees (
  id int primary key,
  data xml
);
```

Insert some sample XML data:

```sql
INSERT INTO employees VALUES
  (1, '<employee>
    <name>Alice Johnson</name>
    <title>Senior Engineer</title>
    <department>Engineering</department>
  </employee>'),
  (2, '<employee>
    <name>Bob Smith</name>
    <title>Product Manager</title>
    <department>Product</department>
  </employee>');
```

## Constructing XML

Use the  `xmlelement()` function to build XML elements programmatically:

```sql
SELECT xmlelement(name employee,
  xmlelement(name name, 'Charlie Brown'),
  xmlelement(name title, 'Designer'),
  xmlelement(name department, 'Design')
);
```

This produces an XML element:

```output
                        xmlelement
------------------------------------------------------
 <employee><name>Charlie Brown</name><title>Designer</title><department>Design</department></employee>
```

### Building XML with attributes

Use `xmlattributes()` to add attributes to elements:

```sql
SELECT xmlelement(name employee,
  xmlattributes(123 as id, 'active' as status),
  xmlelement(name name, 'David Lee')
);
```

This creates an element with attributes:

```output
              xmlelement
-----------------------------------------
 <employee id="123" status="active">
  <name>David Lee</name>
 </employee>
```

### Creating XML comments

Use the `xmlcomment()` function to adds comments:

```sql
SELECT xmlconcat(
  xmlcomment('Employee records'),
  xmlelement(name employees,
    xmlelement(name employee, 'Alice')
  )
);
```

## Querying XML with XPath

Use the `xpath()` function to query XML documents using XPath expressions:

```sql
SELECT id, xpath('/employee/name/text()', data) as name
FROM employees;
```

This extracts the name from each employee XML document:

```output
 id |        name
----+---------------------
  1 | {Alice Johnson}
  2 | {Bob Smith}
```

### Testing XPath matches

Use `xpath_exists()` to test if an XPath expression matches:

```sql
SELECT id FROM employees
WHERE xpath_exists('/employee[title = "Senior Engineer"]', data);
```

This returns employees with the title "Senior Engineer":

```output
 id
----
  1
```

## Converting XML to tables with XMLTABLE

Use the `XMLTABLE` construct to extract tabular data from XML. For example, the given XML contains multiple employee records:

```sql
SELECT * FROM xmltable(
  '/employees/employee'
  passing '<employees>
    <employee id="1" dept="Eng">
      <name>Alice</name>
    </employee>
    <employee id="2" dept="Prod">
      <name>Bob</name>
    </employee>
  </employees>'
  columns
    id int path '@id',
    dept text path '@dept',
    name text path 'name'
);
```

This extracts the data as relational tuples:

```output
 id | dept | name
----+------+-------
  1 | Eng  | Alice
  2 | Prod | Bob
```

## Converting tables to XML

Use the `table_to_xml()` function to convert an entire table to XML:

```sql
SELECT table_to_xml('employees', false, false, '');
```

This creates XML from the table structure.

You can also use `query_to_xml()` to convert query results to XML:

```sql
SELECT query_to_xml(
  'SELECT id, data FROM employees ORDER BY id',
  false, false, ''
);
```

## Aggregating XML

Use the `xmlagg()` function to combine multiple XML values:

```sql
SELECT xmlagg(data ORDER BY id)
FROM employees;
```

This concatenates all employee XML documents into a single XML value:

```output
                              xmlagg
--------------------------------------
 <employee>...</employee>
 <employee>...</employee>
```

## XML predicates

Test document structure with `IS DOCUMENT`:

```sql
SELECT '<root>content</root>'::xml IS DOCUMENT;  -- true
SELECT 'text only' IS NOT DOCUMENT;              -- true
```

## Parsing and serializing XML

### Parse text to XML

Use `xmlparse()` to convert text to XML:

```sql
SELECT xmlparse(content '<root><child>value</child></root>');
```

### Serialize XML to text

Use `xmlserialize()` to convert XML to text:

```sql
SELECT xmlserialize(content data as text) FROM employees;
```

## Namespace support

XPath queries support XML namespaces. When querying namespaced XML, pass the namespace mapping:

```sql
SELECT xpath(
  '//loc:piece/@id',
  '<data xmlns:loc="http://example.com/loc">
    <loc:piece id="1">Item</loc:piece>
  </data>'::xml,
  ARRAY[ARRAY['loc', 'http://example.com/loc']]
);
```

## Performance considerations

- XML parsing and XPath evaluation can be CPU-intensive for large documents.
- For frequent XPath queries on the same column, consider using functional indexes.
- Use `XMLTABLE` to convert XML to relational form for more efficient querying.
- Store frequently queried subvalues as separate columns alongside the XML document.

## Read more

- [XML data type reference](../../../api/ysql/datatypes/type_xml/)
- [PostgreSQL XML documentation](https://www.postgresql.org/docs/current/datatype-xml.html)
- [PostgreSQL XML functions reference](https://www.postgresql.org/docs/current/functions-xml.html)
