---
paths:
  - "src/yb/master/**"
---

# Catalog manager changes

You are changing master code.  Changes to catalog objects (namespaces,
tables, tablets, and other copy-on-write (COW) objects) here commonly cause
deadlocks, objects stuck after a master leader failover, and master leader
stalls.

Before you add or move a lock acquisition, a sys catalog write, or a step of a
multi-step operation, read the "Catalog manager changes" section of
`REVIEW.md` and follow it.  It covers the required lock order (leader shared
lock -> `CatalogManager::mutex_` -> namespace -> table -> tablet COW locks,
each kind sorted by id), the rule to commit COW write locks in the reverse
order of acquisition, resumption after failover between persisted steps,
deadlocks with concurrent operations, and keeping `CatalogManager::mutex_` out
of RPCs and IO.
