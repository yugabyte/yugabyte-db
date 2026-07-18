-- NOTE: No changes to the core extension code contained in this update. This file is only here for version upgrade continuity.

-- Fixed infinite loop in reapply_indexes.py if the same index name would have been used more than once. Thanks to bougyman for tha bug report.
-- Fixed indexes being applied to the parent table instead of the children in reapply_indexes.py (Github Push Request #73).

