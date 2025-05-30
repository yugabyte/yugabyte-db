--
-- Drops enums from yb/integration-tests/xcluster/sql/create_enum.sql
--

--
-- If we drop the original generic one, we don't have to qualify the type
-- anymore, since there's only one match
--
DROP FUNCTION echo_me(anyenum);
DROP FUNCTION echo_me(rainbow);

DROP TABLE enumtest_child;
DROP TABLE enumtest_parent;

DROP TYPE bogon;
DROP TYPE bogon2;
DROP TYPE bogon3;

-- this also drops DOMAIN rgb and TABLE enumtest.
DROP TYPE rainbow CASCADE;

DROP TYPE colors;
DROP TYPE paint_color;

DROP TYPE empty_enum;

DROP TYPE huge_label;

DROP TYPE schema1.enum_in_schema;
DROP SCHEMA schema2 CASCADE;
