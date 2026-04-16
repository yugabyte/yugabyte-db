set search_path to documentdb_core;

-- show all functions exported in documentdb_core.
\df documentdb_core.*

-- show all aggregates exported
\da+ documentdb_core.*

-- Access methods + Operator families
\dA *documentdb*

\dAc * *documentdb*

\dAf * *documentdb*

\dX *documentdb*

-- This is last (Tables/indexes)
\d documentdb_core.*
