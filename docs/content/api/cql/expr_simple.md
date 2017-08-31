---
title: Simple Expressions
summary: Columns, constants, and null.
toc: false
---
<style>
table {
  float: left;
}
#psyn {
  text-indent: 50px;
}
#psyn2 {
  text-indent: 100px;
}
#ptodo {
  color: red
}
</style>

Simple expression can be either a column, a constant, or NULL.

## Column Expression

A column expression refers to a column in a table by using its name, which can be either a fully qualifiedname or a simple name.  
<p id=psyn>
column_expression::= `[keyspace_name.][table_name.][column_name]`
</p>

## Constant Expression

A constant expression represents a simple value by using literals.  
<p id=psyn>
constant_expression::= `string | number`
</p>

## NULL

When an expression, typically a column, does not have a value, it is represented as NULL.  
<p id=psyn>
null_expression::=`NULL`
</p>

## See Also
[All Expressions](..#expressions)