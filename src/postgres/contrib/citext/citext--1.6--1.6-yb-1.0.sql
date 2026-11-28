/* contrib/citext/citext--1.6--1.6-yb-1.0.sql */

-- complain if script is sourced in psql, rather than via ALTER EXTENSION
\echo Use "ALTER EXTENSION citext UPDATE TO '1.6-yb-1.0'" to load this file. \quit

/*
 * YB: the type cache looks up a type's default ordering and equality through
 * its default lsm operator class.  Without one, citext falls back to lsm
 * text_ops through the binary-coercible cast to text, so ORDER BY, GROUP BY
 * and DISTINCT compare bytes instead of using citext_cmp (#36118).  Indexes
 * on citext stay unsupported (#9698), so DocDB never orders by this class.
 */

/* Adopted from contrib/citext/citext--1.4.sql */

CREATE OPERATOR CLASS citext_ops
DEFAULT FOR TYPE citext USING lsm AS
    OPERATOR    1   <  (citext, citext),
    OPERATOR    2   <= (citext, citext),
    OPERATOR    3   =  (citext, citext),
    OPERATOR    4   >= (citext, citext),
    OPERATOR    5   >  (citext, citext),
    FUNCTION    1   citext_cmp(citext, citext);
