-- NOTE: No changes to the core extension code contained in this update. This file is only here for version upgrade continuity.

-- Fixed reapply_indexes.py to work properly with native partitioning. Previously it would recreate all indexes on the template table. If you've run this script on a natively partitioned set already, please review the indexes that exist on your template table and ensure only the ones you need exist. You will likely have to re-run the script again to get the child indexes set properly and you will need to set the the --recreate_all option. If your primary key has changed, also set the --primary option. Thanks to Jawshua on Github for finding the bug and providing a fix. (Github Pull Request #206).

-- Added note to documentation (pg_partman.md) about support for the new IDENTITY feature in PG10.
