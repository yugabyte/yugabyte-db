---
title: INET
summary: IP Address String.
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

`INET` datatype is used to specify columns for data of IP addresses.

## Syntax
```
type_specification ::= { INET }
```

## Semantics
<li>Implicitly, values of type `INET` datatypes are neither convertible nor comparable to other datatypes.</li>
<li>Value of text datatypes with correct format are convertible to `INET`.</li>

## Examples
``` sql
cqlsh:example> CREATE TABLE dns_table(site_name TEXT PRIMARY KEY, ip_address INET);
cqlsh:example> INSERT INTO dns_table(site_name, ip_address) VALUES ('localhost', '127.0.0.1');
cqlsh:example> INSERT INTO dns_table(site_name, ip_address) VALUES ('example.com', '93.184.216.34'); 
cqlsh:example> -- `INET` type supports both ipv4 and ipv6 addresses.
cqlsh:example> UPDATE dns_table SET ip_address = '2606:2800:220:1:248:1893:25c8:1946' WHERE site_name = 'example.com'; 
cqlsh:example> SELECT * FROM dns_table;

 site_name   | ip_address
-------------+------------------------------------
 example.com | 2606:2800:220:1:248:1893:25c8:1946
   localhost |                          127.0.0.1
```

## See Also

[Data Types](..#datatypes)
