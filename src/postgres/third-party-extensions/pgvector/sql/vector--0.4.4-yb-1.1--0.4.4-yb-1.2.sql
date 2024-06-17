\echo Use "ALTER EXTENSION vector UPDATE TO '0.4.4-yb-1.2'" to load this file. \quit

CREATE FUNCTION ybdummyannhandler(internal) RETURNS index_am_handler
	AS 'MODULE_PATHNAME' LANGUAGE C;

CREATE ACCESS METHOD ybdummyann TYPE INDEX HANDLER ybdummyannhandler;

COMMENT ON ACCESS METHOD ybdummyann IS 'ybdummyann index access method';

CREATE OPERATOR CLASS vector_l2_ops
	DEFAULT FOR TYPE vector USING ybdummyann AS
	OPERATOR 1 <-> (vector, vector) FOR ORDER BY float_ops,
	FUNCTION 1 vector_l2_squared_distance(vector, vector);
