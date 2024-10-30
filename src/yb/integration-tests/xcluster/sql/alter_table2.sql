--
-- ALTER_TABLE
--

-- Taken from Postgres test/regress/sql/alter_table.sql
-- Test basic commands.

DROP TABLE attmp;

DROP TABLE attmp_rename_new;  -- fail
DROP TABLE attmp_rename_new2;

DROP INDEX part_attmp_index;
DROP INDEX part_attmp1_index;
DROP TABLE part_attmp;  -- fail
DROP TABLE part_attmp1;  -- fail
DROP TABLE part_at2tmp1;
DROP TABLE part_at2tmp;
