---
title: CREATE FOREIGN DATA WRAPPER statement [YSQL]
headerTitle: CREATE FOREIGN DATA WRAPPER
linkTitle: CREATE FOREIGN DATA WRAPPER
description: Use the CREATE FOREIGN DATA WRAPPER statement to create a foreign-data wrapper.
menu:
  stable:
    identifier: ddl_create_foreign_data_wrapper
    parent: statements
type: docs
---

## Synopsis

Use the `CREATE FOREIGN DATA WRAPPER` statement to create a foreign data wrapper.
Only superusers or users with the yb_fdw role can create foreign data wrappers.

## Syntax

{{%ebnf%}}
  create_foreign_data_wrapper
{{%/ebnf%}}

## Semantics

Create a FDW named *fdw_name*.

### Handler function
The *handler_function* will be called to retrieve the execution functions for foreign tables. These functions are required by the planner and executor.
The handler function takes no arguments and its return type should be `fdw_handler`.
If no handler function is provided, foreign tables that use the wrapper can only be declared (and not accessed).

### Validator function

The *validator_function* is used to validate the options given to the foreign-data wrapper, and the foreign servers, user mappings and foreign tables that use the foreign-data wrapper.
The validator function takes two arguments: a text array (type text[]) that contains the options to be validated, and an OID of the system catalog that the object associated with the options is stored in.
If no validator function is provided (or `NO VALIDATOR` is specified), the options will not be checked at the time of creation.

### Options:
The `OPTIONS` clause specifies options for the foreign-data wrapper. The permitted option names and values are specific to each foreign data wrapper. The options are validated using the FDW’s validator function.

## Examples

Basic example.

```plpgsql
yugabyte=# CREATE FOREIGN DATA WRAPPER my_wrapper HANDLER myhandler OPTIONS (dummy 'true');
```

## See also

- [`CREATE FOREIGN TABLE`](../ddl_create_foreign_table/)
- [`CREATE SERVER`](../ddl_create_server/)
- [`CREATE USER MAPPING`](../ddl_create_user_mapping/)
- [`IMPORT FOREIGN SCHEMA`](../ddl_import_foreign_schema/)
- [`ALTER FOREIGN DATA WRAPPER`](../ddl_alter_foreign_data_wrapper/)
- [`DROP FOREIGN DATA WRAPPER`](../ddl_drop_foreign_data_wrapper/)
