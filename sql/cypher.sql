LOAD 'agensgraph';
SET search_path TO ag_catalog;

SELECT create_graph('cypher');

-- cypher() function takes only a dollar-quoted string constant as an argument.
-- All other cases throw an error.

SELECT * FROM cypher('none', $$RETURN 0$$) as q(c agtype);

SELECT * FROM cypher('cypher', $$RETURN 0$$) AS r(c agtype);
WITH r(c) AS (
  SELECT * FROM cypher('cypher', $$RETURN 0$$) AS r(c agtype)
)
SELECT * FROM r;
SELECT * FROM cypher('cypher', 'RETURN 0') AS r(c text);
SELECT * FROM cypher('cypher', NULL) AS r(c text);
WITH q(s) AS (
  VALUES (textout($$RETURN 0$$))
)
SELECT * FROM q, cypher('cypher', q.s) AS r(c text);

-- The numbers of the attributes must match.

SELECT * FROM cypher('cypher', $$RETURN 0$$) AS r(c text, x text);

-- cypher() function can be called in ROWS FROM only if it is there solely.

SELECT * FROM ROWS FROM (cypher('cypher', $$RETURN 0$$) AS (c agtype));
SELECT * FROM ROWS FROM (cypher('cypher', $$RETURN 0$$) AS (c text),
                         generate_series(1, 2));

-- WITH ORDINALITY is not supported.

SELECT *
FROM ROWS FROM (cypher('cypher', $$RETURN 0$$) AS (c text)) WITH ORDINALITY;

-- cypher() function cannot be called in expressions.
-- However, it can be called in subqueries.

SELECT cypher('cypher', $$RETURN 0$$);
SELECT (SELECT * FROM cypher('cypher', $$RETURN 0$$) AS r(c agtype));

-- Attributes returned from cypher() function are agtype. If other than agtype
-- is given in the column definition list and there is a type coercion from
-- agtype to the given type, agtype will be coerced to that type. If there is
-- not such a coercion, an error is thrown.

SELECT * FROM cypher('cypher', $$RETURN true$$) AS (c bool);
SELECT * FROM cypher('cypher', $$RETURN 0$$) AS (c oid);

SELECT drop_graph('cypher');
