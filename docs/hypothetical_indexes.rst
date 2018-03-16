.. _hypothetical_indexes:

Hypothetical Indexes
====================

An hypothetical, or virtual, index is an index that doesn't really exists, and
thus doesn't cost CPU, disk or any resource to create.  They're useful to know
if specific indexes can increase performance for problematic queries, since
you can know if PostgreSQL will use these indexes or not without having to
spend resources to create them.
