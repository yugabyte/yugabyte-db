TODO
====

Important
---------

- [X] Choose a better naming convention, including the index oid
- [X] handle multiple columns
- [X] handle collation
- [ ] handle GIN access method
- [ ] handle GiST access method
- [ ] handle SP-GiST access method
- [X] handle BRIN access method
- [X] better formula for number of pages in index
- [ ] handle tree height
- [X] Add check for btree: total column size must not exceed BTMaxItemSize (maybe less, just in case?)
- Add some more (or enhance) function. Following are interesting:
- [X] estimated index size
- [ ] estimated number of lines
- [X] add hypopg_get_indexdef(oid)

Less important
--------------

- [ ] specify tablespace
- [ ] Compatibility PG 9.2-
- [X] handle unique index
- [X] handle reverse and nulls first
- [X] handle index on expression
- [X] handle index on predicate
- [ ] specify a bloat factor
