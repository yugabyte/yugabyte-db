-- ALTER AGGREGATE
ALTER AGGREGATE my_sum(integer) RENAME TO my_total;

-- ALTER CAST   --- Not Supported on YB yet

-- ALTER COLLATION
ALTER COLLATION my_collation RENAME TO custom_collation;

-- ALTER CONVERSION  --- Not Supported on YB yet

-- ALTER DOMAIN  --- Not Supported on YB yet
-- ALTER DOMAIN positive_int
--    ADD CONSTRAINT check_less_than_1000 CHECK (VALUE < 1000);

-- ALTER FUNCTION
ALTER FUNCTION add_two_numbers(a INTEGER, b INTEGER)
    RENAME TO sum_two_numbers;

-- ALTER OPERATOR
ALTER OPERATOR === (INTEGER, INTEGER)
    OWNER TO yugabyte;

-- ALTER OPERATOR CLASS --- Not Supported on YB yet
-- ALTER OPERATOR CLASS text_length_ops USING btree
--    RENAME TO text_length_order_ops;

-- ALTER OPERATOR FAMILY
ALTER OPERATOR FAMILY int_fam USING btree
    RENAME TO integer_family;

-- ALTER POLICY
ALTER POLICY my_policy ON employees
    RENAME TO hr_select_policy;

-- ALTER PROCEDURE --- Not Supported on YB yet
-- ALTER PROCEDURE increment_salary(integer, integer) RENAME TO increase_salary;

-- ALTER RULE --- Not Supported on YB yet
-- ALTER RULE replace_insert ON employees
-- RENAME TO insert_default_user;

-- ALTER SCHEMA
ALTER SCHEMA company_schema
    RENAME TO corp_schema;

-- ALTER STATISTICS --- Not Supported on YB yet
-- ALTER STATISTICS names_distinct
--    SET STATISTICS 100;

-- ALTER TRIGGER
ALTER TRIGGER update_timestamp ON employees
    RENAME TO set_last_modified_timestamp;

-- ALTER VIEW
ALTER VIEW employee_salaries
    RENAME TO emp_salary_view;


--------------Text Search DDLs --------------------------

ALTER TEXT SEARCH TEMPLATE simple_template RENAME TO renamed_template;

ALTER TEXT SEARCH PARSER simple_parser RENAME TO renamed_parser;

ALTER TEXT SEARCH DICTIONARY simple_dict RENAME TO renamed_dict;

ALTER TEXT SEARCH CONFIGURATION simple_config RENAME TO renamed_config;
