---
title: INET
summary: IP Address String
description: INET Type
menu:
  v1.0:
    parent: api-cassandra
    weight: 1410
aliases:
  - api/cassandra/type_inet
  - api/cql/type_inet
  - api/ycql/type_inet
---

## Synopsis

`INET` datatype is used to specify columns for data of IP addresses.

## Syntax
```
type_specification ::= INET
```

## Semantics

- Columns of type `INET` can be part of the `PRIMARY KEY`.
- Implicitly, values of type `INET` datatypes are neither convertible nor comparable to other datatypes.
- Values of text datatypes with correct format are convertible to `INET`.
- `INET` value format supports text literals for both IPv4 and IPv6 addresses.

## Examples
```{.sql .copy .separator-gt}
cqlsh:example> CREATE TABLE dns_table(site_name TEXT PRIMARY KEY, ip_address INET);
```
```{.sql .copy .separator-gt}
cqlsh:example> INSERT INTO dns_table(site_name, ip_address) VALUES ('localhost', '127.0.0.1');
```
```{.sql .copy .separator-gt}
cqlsh:example> INSERT INTO dns_table(site_name, ip_address) VALUES ('example.com', '93.184.216.34'); 
```
`INET` type supports both ipv4 and ipv6 addresses.
```{.sql .copy .separator-gt}
cqlsh:example> UPDATE dns_table SET ip_address = '2606:2800:220:1:248:1893:25c8:1946' WHERE site_name = 'example.com'; 
```
```{.sql .copy .separator-gt}
cqlsh:example> SELECT * FROM dns_table;
```
```sh
 site_name   | ip_address
-------------+------------------------------------
 example.com | 2606:2800:220:1:248:1893:25c8:1946
   localhost |                          127.0.0.1
```

## See Also

[Data Types](..#datatypes)
