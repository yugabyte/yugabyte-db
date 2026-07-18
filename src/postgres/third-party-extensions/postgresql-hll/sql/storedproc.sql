-- ----------------------------------------------------------------
-- Establishes global setting behavior with stored procedures.
-- ----------------------------------------------------------------

-- Outside stored procedure.

SELECT hll_set_max_sparse(256);
SELECT hll_set_defaults(10,4,128,0);

-- When defining a stored procedure the global statements don't take
-- effect.

CREATE OR REPLACE FUNCTION testfunc_azfwygmg() RETURNS void AS $$
BEGIN
PERFORM hll_set_max_sparse(-1);
PERFORM hll_set_defaults(11,5,-1,1);
END;
$$ LANGUAGE plpgsql;

SELECT hll_set_max_sparse(256);
SELECT hll_set_defaults(10,4,128,0);

-- When invoking a stored procedure they work.

SELECT testfunc_azfwygmg();

SELECT hll_set_max_sparse(256);
SELECT hll_set_defaults(10,4,128,0);
