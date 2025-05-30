--
-- Yugabyte-owned test for ybgin index access method and null categories.
--

-- GIN_CAT_NULL_KEY
INSERT INTO arrays (a) VALUES ('{null}');
SELECT * FROM arrays WHERE a @> '{null}';
-- GIN_CAT_EMPTY_ITEM
INSERT INTO arrays (a) VALUES ('{}');
-- GIN_CAT_NULL_ITEM
INSERT INTO arrays (a) VALUES (null);
