LOAD 'agensgraph';
SET search_path TO ag_catalog;

--
-- map literal
--

-- empty map
SELECT * FROM cypher($$RETURN {}$$) AS r(c jsonbx);

-- map of scalar values
SELECT * FROM cypher($$
RETURN {s: 's', i: 1, f: 1.0, b: true, z: null}
$$) AS r(c jsonbx);

--
-- list literal
--

-- empty list
SELECT * FROM cypher($$RETURN []$$) AS r(c jsonbx);

-- list of scalar values
SELECT * FROM cypher($$RETURN ['str', 1, 1.0, true, null]$$) AS r(c jsonbx);
