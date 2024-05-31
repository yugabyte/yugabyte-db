.. _hypothetical_indexes:

Hypothetical Indexes
====================

A hypothetical, or virtual, index is an index that does not really exist, and
therefore does not cost CPU, disk or any resource to create. They are useful to
find out whether specific indexes can increase the performance for problematic
queries, since you can discover if PostgreSQL will use these indexes or not
without having to spend resources to create them.
