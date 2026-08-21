-- Per-P BitmapScan hint: set :hint from the current :P, then iterate Q.
-- Callers set Q2 to ':hint'.  Unquoted so :P is expanded (\set does not
-- expand variables in single quotes).
\set hint /*+ BitmapScan(:P) */
\i :_iter_Q2
