--
-- Drops enums from yb/integration-tests/xcluster/sql/create_type.sql
--

--
-- Drop shell type
--
DROP TYPE shell;


--
-- Drop composite type
--
DROP TYPE composite_type;
DROP TYPE composite_type2;


--
-- Drop enum type
--
DROP TYPE empty_enum;


--
-- Drop range type
--
DROP TYPE range_type;

DROP TYPE two_ints CASCADE;

DROP TYPE textrange1;
DROP TYPE textrange2;

DROP TYPE range_type_in_two_steps;


--
-- Drop base type
--
DROP TYPE base_type CASCADE;


--
-- Composite types with the same name but different schemas
--
DROP TYPE schema1.composite_type_in_schema;
DROP TYPE schema2.composite_type_in_schema;


-- Types with weird names
DROP TYPE "funny type +";
DROP TYPE "inside ""quotes""!";
DROP TYPE "CREATE";
