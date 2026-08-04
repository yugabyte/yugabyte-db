-- Unique index and unique constraint. Must fail setting non-unique index as constraint.
CREATE TABLE tbl_include_non_unique (c1 int, c2 int, c3 int, c4 int);
CREATE INDEX tbl_include_non_unique_idx on tbl_include_non_unique (c1);
ALTER TABLE tbl_include_non_unique ADD CONSTRAINT constr_non_unique UNIQUE USING INDEX tbl_include_non_unique_idx;

-- Unique index and unique constraint. Must fail setting DESC unique index as constraint.
CREATE TABLE tbl_include_desc_unique (c1 int, c2 int, c3 int, c4 int);
CREATE UNIQUE INDEX tbl_include_desc_unique_idx on tbl_include_desc_unique (c1 DESC);
ALTER TABLE tbl_include_desc_unique ADD CONSTRAINT constr_desc_unique UNIQUE USING INDEX tbl_include_desc_unique_idx;
