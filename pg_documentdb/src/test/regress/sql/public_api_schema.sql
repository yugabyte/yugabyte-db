-- show all functions exported in documentdb_api.
\df documentdb_api.*

\df documentdb_api_catalog.*

\df documentdb_data.*

-- show all aggregates exported
\da+ documentdb_api.*

\da+ documentdb_api_catalog.*

\da+ documentdb_data.*

-- Access methods + Operator families
\dA *documentdb*

\dAc *documentdb*

\dAf *documentdb*

\dX *documentdb*

-- This is last (Tables/indexes)
\d documentdb_api.*

\d documentdb_api_catalog.*
