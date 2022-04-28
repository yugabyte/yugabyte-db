--
-- Test case for unsupported collations
--
CREATE TABLE collate_test_fail(b text COLLATE "nds-x-icu");

CREATE COLLATION nds from "nds-x-icu";

CREATE TABLE collate_test_fail (a int, b text);
SELECT * from collate_test_fail where b like 'a'  COLLATE "nds-x-icu";
