### documentdb v0.102-0 (Unreleased) ###
* Support index pushdown for vector search queries *[Bugfix]*
* Support exact search for vector search queries *[Feature]*
* Inline $match with let in $lookup pipelines as JOIN Filter *[Perf]*
* Support TTL indexes *[Bugfix]* (#34)
* Support joining between postgres and documentdb tables *[Feature]* (#61)
* Support current_op command *[Feature]* (#59)
* Support for list_databases command *[Feature]* (#45)
* Disable analyze statistics for unique index uuid columns which improves resource usage *[Perf]*
* Support collation with `$expr`, `$in`, `$cmp`, `$eq`, `$ne`, `$lt`, `$lte`, `$gt`, `$gte` comparison operators (Opt-in) *[Feature]*
* Support collation in `find`, aggregation `$project`, `$redact`, `$set`, `$addFields`, `$replaceRoot` stages (Opt-in) *[Feature]*
* Support collation with `$setEquals`, `$setUnion`, `$setIntersection`, `$setDifference`, `$setIsSubet` in the aggregation pipeline (Opt-in) *[Feature]*
* Support unique index truncation by default with new operator class *[Feature]*
* Top level aggregate command `let` variables support for `$geoNear` stage *[Feature]*
* Enable Backend Command support for Statement Timeout *[Feature]*
* Support type aggregation operator `$toUUID`. *[Feature]* 
* Support Partial filter pushdown for `$in` predicates *[Perf]*
* Support the $dateFromString operator with full functionality *[Feature]*
* Support extended syntax for `$getField` aggregation operator. Now the value of 'field' could be an expression that resolves to a string. *[Feature]*

### documentdb v0.101-0 (February 12, 2025) ###
* Push $graphlookup recursive CTE JOIN filters to index *[Perf]*
* Build pg_documentdb for PostgreSQL 17 *[Infra]* (#13)
* Enable support of currentOp aggregation stage, along with collstats, dbstats, and indexStats *[Commands]* (#52)
* Allow inlining $unwind with $lookup with `preserveNullAndEmptyArrays` *[Perf]*
* Skip loading documents if group expression is constant *[Perf]* 
* Fix Merge stage not outputing to target collection *[Bugfix]* (#20)

### documentdb v0.100-0 (January 23rd, 2025) ###
Initial Release 