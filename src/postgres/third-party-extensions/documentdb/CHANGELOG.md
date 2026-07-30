### documentdb v0.109-0 (Unreleased) ###
* Support collation with find positional queries *[Feature]*
* Short-circuit in `$cond` runtime evaluation *[Perf]*
* Support operator variables(eg: $map.as alias) in let variable spec *[Bugfix]*
* Support for `killOp` administrative command *[Feature]*
* Fix `$addToSet` behavior and skip the top-level field rewrite because it's already done in the operator *[Bugfix]*
* Performance improvements for $addToSet update operator up to ~70x for large existing and update arrays. *[Perf]*
* Removed feature flags `documentdb.enableCompact`, `documentdb.enableBucketAutoStage` and `documentdb.enableIndexHintSupport`
* Fix use-after-free segmentation fault in `$let` *[Bugfix]* 
* Short-circuit in `$switch` at parse time *[Perf]*
* Enable ordered indexes by default. Can be turned off by specifying "storageEngine": {"enableOrderedIndex": false} for a single index or by turning off the `documentdb.defaultUseCompositeOpClass` GUC.
* Fix NULL document crash from `$in: []` optimization on sharded collections.

### documentdb v0.108-0 (Unreleased) ###
* Top-level `let` variables and `$$NOW` supported by default.
* Fix collation support on find and aggregation when variableSpec is not available *[Bugfix]*.
* Support `dropRole` command *[Feature]*
* Support `rolesInfo` command *[Feature]*
* Fix concurrent upsert behavior, update the documents in case of conflicts during insert *[Bugfix]* (#295).
* Support collation with `$sortArray` aggregation operator *[Feature]*
* Add support for keyword `required` in `$jsonSchema`
* Fix a segmentation fault when using ordered aggregate such as `$last` with `$setWindowFields` aggregation stage. *[Bugfix]*
* Fix crash when building lookup pipeline queries from nested pipelines and $group aggregates *[Bugfix]*
* Add basic support for compiling with pg18 *[Feature]*
* Drop unused environment variable `ENFORCE_SSL` in dockerfile *[Bugfix]* (#313)
* Remove the explicit dependency on the RUM extension (it's now implicit on the .so file). Flip to documentdb_extended_rum for PG18+ *[Feature]*
* Use the appropriate GUC for the user_crud_commands.sql *[Bugfix]* (#319)
* Provide Rust dev environment in devcontainer *[Feature]*
* Add extension that adds a gateway host that's run as a postgres background worker *[Feature]*

### documentdb v0.107-0 (Unreleased) ###
* Support sort by _id against the _id index using the enableIndexOrderbyPushdown flag *[Feature]*.
* Improvements to explain for various scan types *[Feature]*.
* Support schema enforcement with CSFLE integration *[Preview]*
* Validate $jsonSchema syntax during rule creation or modification(schema validation) *[Preview]*

### documentdb v0.106-0 (August 29, 2025) ###
* Add internal extension that provides extensions to the `rum` index. *[Feature]*
* Enable let support for update queries *[Feature]*. Requires `EnableVariablesSupportForWriteCommands` to be `on`.
* Enable let support for findAndModify queries *[Feature]*. Requires `EnableVariablesSupportForWriteCommands` to be `on`.
* Add internal extension that provides extensions to the `rum` index. *[Feature]*
* Optimized query for `usersInfo` command.
* Support collation with `delete` *[Feature]*. Requires `EnableCollation` to be `on`.
* Support for index hints for find/aggregate/count/distinct *[Feature]*
* Support `createRole` command *[Feature]*
* Add schema changes for Role CRUD APIs *[Feature]*
* Add support for using EntraId tokens via Plain Auth

### documentdb v0.105-0 (July 28, 2025) ###
* Support `$bucketAuto` aggregation stage, with granularity types: `POWERSOF2`, `1-2-5`, `R5`, `R10`, `R20`, `R40`, `R80`, `E6`, `E12`, `E24`, `E48`, `E96`, `E192` *[Feature]*
* Support `conectionStatus` command *[Feature]*.

### documentdb v0.104-0 (June 09, 2025) ###
* Add string case support for `$toDate` operator
* Support `sort` with collation in runtime*[Feature]*
* Support collation with `$indexOfArray` aggregation operator. *[Feature]*
* Support collation with arrays and objects comparisons *[Feature]*
* Support background index builds *[Bugfix]* (#36)
* Enable user CRUD by default *[Feature]*
* Enable let support for delete queries *[Feature]*. Requires `EnableVariablesSupportForWriteCommands` to be `on`.
* Enable rum_enable_index_scan as default on *[Perf]*
* Add public `documentdb-local` Docker image with gateway to GHCR
* Support `compact` command *[Feature]*. Requires `documentdb.enablecompact` GUC to be `on`.
* Enable role privileges for `usersInfo` command *[Feature]* 

### documentdb v0.103-0 (May 09, 2025) ###
* Support collation with aggregation and find on sharded collections *[Feature]*
* Support `$convert` on `binData` to `binData`, `string` to `binData` and `binData` to `string` (except with `format: auto`) *[Feature]*
* Fix list_databases for databases with size > 2 GB *[Bugfix]* (#119)
* Support half-precision vector indexing, vectors can have up to 4,000 dimensions *[Feature]*
* Support ARM64 architecture when building docker container *[Preview]*
* Support collation with `$documents` and `$replaceWith` stage of the aggregation pipeline *[Feature]*
* Push pg_documentdb_gw for documentdb connections *[Feature]*

### documentdb v0.102-0 (March 26, 2025) ###
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
* Support collation with `$setEquals`, `$setUnion`, `$setIntersection`, `$setDifference`, `$setIsSubset` in the aggregation pipeline (Opt-in) *[Feature]*
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
