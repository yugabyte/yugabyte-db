-- Per-P BitmapScan hint: set Q2 from current :P, then iterate Q.
-- Unquoted so :P is expanded (\set does not expand variables in single quotes).
\set Q2 /*+ BitmapScan(:P) */
\i :iter_Q2
