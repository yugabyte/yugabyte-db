---
title: Collection
summary: Collection types.
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

Collection datatypes are used to specify columns for data objects that can contains more than one values.

### LIST
`LIST` is an ordered collection of elements. All elements in a `LIST` must be of the same primitive types. Elements can be prepend or append by `+` operator to a list, removed by `-` operator, and referenced by their indexes of that list by `[]` operator.

### MAP
`MAP` is an unordered collection of pairs of elements, a key and a value. With their key values, elements in a `MAP` can be set by the `[]` operator, added by the `+` operator, and removed by the `-` operator.

### SET
`SET` is a sorted collection of elements. The sorting order is implementation-dependent. Elements can be added by `+` operator and removed by `-` operator. When queried, the elements of a set will be returned in sorting order.

## Syntax
```
type_specification ::= { LIST<type> | MAP<key_type:type> | SET<type> }
```

## Semantics

### Restrictions

<li> Type parameters must be simple types or [frozen types](../type_frozen).
 <small> (Collections and user-defined types must be frozen to be used as collection parameters) </small></li>
<li>Columns of type `LIST`, `MAP`, and `SET` cannot be part of `PRIMARY KEY`.</li>
<li>Implicitly, values of collection datatypes are neither convertible nor comparable to other datatypes.</li>

### Properties
<li> Comparisons on collection values are not allowed. </li>
<li> Empty collections are treated as null values. </li>

## Examples

### `CREATE TABLE` with Collections.
- Collection types are used like simple types (except they are not allowed in primary key).

``` sql
cqlsh:example> CREATE TABLE users(username TEXT PRIMARY KEY, 
                                  emails SET<TEXT>,
                                  phones MAP<TEXT,TEXT>,
                                  top_cities LIST<TEXT>);
```

### `INSERT` Collection Data.
- Collection values are inserted by setting all their elements at once.

``` sql
cqlsh:example> INSERT INTO users(username, emails, phones, top_cities) 
               VALUES ('foo', 
                       {'c@example.com', 'a@example.com'}, 
                       {'home' : '999-9999', 'mobile' : '000-0000'}, 
                       ['New York', 'Paris']);

cqlsh:example> -- Empty collections are the same as nulls.
cqlsh:example> INSERT INTO users(username, emails, phones, top_cities) VALUES ('bar', { }, { }, [ ]);

cqlsh:example> SELECT * FROM users;

 username | emails                             | phones                                     | top_cities
----------+------------------------------------+--------------------------------------------+-----------------------
      bar |                               null |                                       null |                  null
      foo | {'a@example.com', 'c@example.com'} | {'home': '999-9999', 'mobile': '000-0000'} | ['New York', 'Paris']
```

### `UPDATE` Collection Column.
- Collection values can be updated by setting all their elements at once.

``` sql
cqlsh:example> UPDATE users SET emails = {'bar@example.com'} WHERE username = 'bar';
cqlsh:example> UPDATE users SET phones = {'home' : '123-45678'} WHERE username = 'bar';
cqlsh:example> UPDATE users SET top_cities = ['London', 'Tokyo'] WHERE username = 'bar';

cqlsh:example> SELECT * FROM users;

 username | emails                             | phones                                     | top_cities
----------+------------------------------------+--------------------------------------------+-----------------------
      bar |                {'bar@example.com'} |                      {'home': '123-45678'} |   ['London', 'Tokyo']
      foo | {'a@example.com', 'c@example.com'} | {'home': '999-9999', 'mobile': '000-0000'} | ['New York', 'Paris']
```

### Collection Expressions
- Collection elements can be added with `+` or removed with `-`.

``` sql
cqlsh:example> UPDATE users SET emails = emails + {'foo@example.com'} WHERE username = 'foo';
cqlsh:example> UPDATE users SET emails = emails - {'a@example.com', 'c.example.com'} WHERE username = 'foo';

cqlsh:example> UPDATE users SET phones = phones + {'office' : '333-3333'} WHERE username = 'foo';
cqlsh:example> -- To remove map elements only the relevant keys need to be given (as a set).
cqlsh:example> UPDATE users SET phones = phones - {'home'} WHERE username = 'foo';

cqlsh:example> -- List elements can be either prepended or appended. 
cqlsh:example> UPDATE users SET top_cities = top_cities + ['Delhi'] WHERE username = 'foo';
cqlsh:example> UPDATE users SET top_cities = ['Sunnyvale'] + top_cities WHERE username = 'foo';
cqlsh:example> UPDATE users SET top_cities = top_cities - ['Paris', 'New York'] WHERE username = 'foo';

cqlsh:example> SELECT * FROM users;

 username | emails              | phones                                       | top_cities
----------+---------------------+----------------------------------------------+------------------------
      bar | {'bar@example.com'} |                        {'home': '123-45678'} |    ['London', 'Tokyo']
      foo | {'foo@example.com'} | {'mobile': '000-0000', 'office': '333-3333'} | ['Sunnyvale', 'Delhi']
```

### `UPDATE` Map and List Elements
- Maps and Lists also allow updating elements individually.

``` sql
cqlsh:example> UPDATE users SET phones['mobile'] = '111-1111' WHERE username = 'foo';
cqlsh:example> -- Map elements can also be accessed in IF/WHERE conditions.
cqlsh:example> UPDATE users SET phones['mobile'] = '345-6789' WHERE username = 'bar' IF phones['mobile'] = null;

cqlsh:example> -- List numbering starts from 1.
cqlsh:example> UPDATE users SET top_cities[1] = 'San Francisco' WHERE username = 'bar';
cqlsh:example> -- List elements can also be accessed in IF/WHERE conditions.
cqlsh:example> UPDATE users SET top_cities[2] = 'Mumbai' WHERE username = 'foo' IF top_cities[2] = 'Delhi';

cqlsh:example> SELECT * FROM users;

 username | emails              | phones                                       | top_cities
----------+---------------------+----------------------------------------------+----------------------------------
      bar | {'bar@example.com'} |  {'home': '123-45678', 'mobile': '345-6789'} |       ['San Francisco', 'Tokyo']
      foo | {'foo@example.com'} | {'mobile': '111-1111', 'office': '333-3333'} | ['Mumbai', 'Sunnyvale', 'Delhi']
```

## See Also

[Data Types](..#datatypes)
