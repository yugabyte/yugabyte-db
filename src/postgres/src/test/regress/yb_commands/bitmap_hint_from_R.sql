-- Per-R BitmapScan hint: build hint from current :R, then execute query.
-- Unquoted so :R is expanded (\set does not expand variables in single quotes).
\set hint /*+ BitmapScan(:R) */
\i :_iter_query
