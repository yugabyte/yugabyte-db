TODO
====

Important
---------

- [ ] Choose a naming convention for index naming, including the index oid
- [X] handle multiple columns
- [X] handle collation
- [ ] handle other am other than btree
- [ ] better formula for number of pages in index
- [ ] handle tree height
- [ ] Add some more (or enhance) function. Following are intereseting:
      [ ] estimated index size
      [ ] estimated number of lines
- [ ] add hypopg_get_indexdef(oid) (based on src/backend/utils/adt/ruleutils.c/pg_get_indexdef_worker())

Less important
--------------

- [ ] specify tablespace
- [ ] Compatibility PG 9.2-
- [-] handle unique index, still need to see if there is an impact somewhere...
- [X] handle reverse and nulls first
- [ ] handle index on expression
- [ ] handle index on predicate
- | ] specify a bloat factor
