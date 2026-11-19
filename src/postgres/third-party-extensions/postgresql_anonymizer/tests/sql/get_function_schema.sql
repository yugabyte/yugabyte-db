BEGIN;

CREATE EXTENSION IF NOT EXISTS anon CASCADE;

SELECT anon.get_function_schema(NULL::TEXT) IS NULL;
SELECT anon.get_function_schema('a()') = '';
SELECT anon.get_function_schema('a(1,2,3)') = '';
SELECT anon.get_function_schema('"A"(1,2,3)') = '';
SELECT anon.get_function_schema('a.b()') = 'a';
SELECT anon.get_function_schema('a."b.c"(''c.d'')') = 'a';
SELECT anon.get_function_schema('"A.B".c()') = 'A.B';
SELECT anon.get_function_schema('"A.B"."c.d"()') = 'A.B';
SELECT anon.get_function_schema('"a("(1,2,3)') = '';
SELECT anon.get_function_schema('"..".".."($$..$$)') = '..';
SELECT anon.get_function_schema('".......".a()') = '.......';
SELECT anon.get_function_schema('a."......."()') = 'a';

SAVEPOINT function_call_error_1;
SELECT anon.get_function_schema('a');
ROLLBACK TO function_call_error_1;

SAVEPOINT function_call_error_2;
SELECT anon.get_function_schema('a,b,c');
ROLLBACK TO function_call_error_2;

SAVEPOINT function_call_error_3;
SELECT anon.get_function_schema('a;SELECT b');
ROLLBACK TO function_call_error_3;

ROLLBACK;
