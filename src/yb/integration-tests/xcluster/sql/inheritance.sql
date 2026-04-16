--
-- Testing multilevel inheritance.
--

-- Setting up the multilevel inheritance structure.

CREATE TABLE inherited_parent(a int, b text);


CREATE TABLE inherited_child1(PRIMARY KEY (a ASC)) 
    INHERITS (inherited_parent);
CREATE INDEX idx_child1_b ON inherited_child1(b);

INSERT INTO inherited_child1 VALUES(1, '100');
INSERT INTO inherited_child1 VALUES(2, '20');
INSERT INTO inherited_child1 VALUES(3, '5');


CREATE TABLE inherited_child2(c int) 
    INHERITS (inherited_parent);
CREATE INDEX idx_child2_c ON inherited_child2(b, c);

INSERT INTO inherited_child2 VALUES(4, '67', 8);
INSERT INTO inherited_child2 VALUES(5, '13', 7);


CREATE TABLE inherited_grandchild1(d int)
    INHERITS (inherited_child1);
CREATE INDEX idx_grandchild1_b_d ON inherited_grandchild1(b, d);

INSERT INTO inherited_grandchild1 VALUES(6, '42', -1);



-- Test that alter propagates correctly.

ALTER TABLE inherited_parent 
  ALTER COLUMN b TYPE int USING b::integer;

SELECT * FROM inherited_child1 WHERE b > 10 ORDER BY b;
SELECT * FROM inherited_grandchild1 WHERE b > 10 AND d < 0 ORDER BY b;


-- Test that TRUNCATE propagates correctly.

TRUNCATE TABLE inherited_parent;
