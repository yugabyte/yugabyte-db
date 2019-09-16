LOAD 'agensgraph';
SET search_path TO ag_catalog;

--
-- list literal
--

-- empty list
SELECT * FROM cypher($$RETURN []$$) AS r(c jsonbx);

-- list of scalar values
SELECT * FROM cypher($$RETURN ['str', 1, 1.0, true, null]$$) AS r(c jsonbx);
