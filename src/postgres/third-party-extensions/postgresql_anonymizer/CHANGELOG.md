CHANGELOG
===============================================================================


20240304 : 1.3.1 - Minor release
-------------------------------------------------------------------------------

__Changes:__

* Fix PGXN metadata

20240304 : 1.3.0 - Important Security Update
-------------------------------------------------------------------------------

__Breaking Changes__:

This new version enforces a series of security checks and it will now
refuse some masking rules that were previously accepted. Users of previous
versions may have to rewrite some rules inside their masking policy.

__Changes:__

* [core] improve injection protection for tablesample rules
* [core] Add bindings to useful `pg_catalog` functions
* [core] Masking a view is not supported
* [docs] upgrade version to 1.3
* [core] Checking schemas in nested masking functions
* [core] do not trust `pg_catalog` by default
* [core] Activate `restrict_to_trusted_schemas` by default
* [core] Prevent injection from masking rules
* Prepare for the Rust rewrite
* [core] replace strncpy by strlcpy
* Remove dependency to pgcrypto


20240115 : 1.2.0 - Support for PostgreSQL 16
-------------------------------------------------------------------------------

<!-- https://gitlab.com/dalibo/postgresql_anonymizer/-/milestones/19 -->
<!-- git log --oneline $(git describe --tags --abbrev=0 @^)..@ -->

__Breaking Changes__:

### A new k-anonymity provider

In previous version, the indirect identifiers were declared like this:

  ```sql
  SECURITY LABEL FOR anon ON COLUMN users.id IS 'INDIRECT IDENTIFIER';
  ```

With version 1.2, this rule must be rewritten as follows:

  ```sql
  SECURITY LABEL FOR k_anonymity ON COLUMN users.id IS 'INDIRECT IDENTIFIER';
  ```

For more details, read the "Generalization" section of the doc

<https://postgresql-anonymizer.readthedocs.io/en/latest/generalization/#k-anonymity>

__New features:__

* Introducing a new sampling syntax to anonymize only a smaller portion of a dataset
* Support for PostgreSQL 16
* End of support for PostgreSQL 10 and 11
* New debian packages
* New French fake dataset
* New syntax `COLUMN foo.bar IS 'NOT MASKED'`, useful when privacy_by_default
  is enabled
* Transparent Anonymous Dumps (still in beta)

__Changes:__

* [random] Introduce new 'Random in Range' functions
* [random] Introduce new 'Random in Enum' functions
* [core] Grant SELECT on sequences (#359)
* [core] Do not use @extschema@
* [core] Remove the deprecated standalone script
* [pseudo] Update the fake SIRET table (#339)
* [doc] Clarification on conditional masking (#383)
* [pseudo] Fix anon.pseudo_country (#326)




20220928 : 1.1.0 - Privacy By Default
-------------------------------------------------------------------------------

<!-- https://gitlab.com/dalibo/postgresql_anonymizer/-/milestones/18 -->
<!-- git log --oneline $(git describe --tags --abbrev=0 @^)..@ -->

__Dependencies:__

- pg_crypto

__Changes:__

* [debian] initial draft for a package
* [docker] switch to pg_dump_anon
* [core] register a new masking policy
* [core] New GUC param: anon.masking_policies
* [CI] add test for sequences with uppercase letters
* [pg_dump_anon] FIX #298: export value for uppercase sequences
* [pg_dump_anon] Consistent backups
* [meta] Update copyright date (Gergő Rubint)
* [blackbox] FIX Anon extension is not installed
* [data] update the dictionnary of english identifiers
* [doc] Updates on masking rules (Mahesh Moturu)
* [doc] Fix howto: grant select on supplier (Christophe Courtois)
* [doc] how to contribute to the detect
* [doc] Update to improve document clarity and spelling error
* [doc] Update docker example in INSTALL.md (@bojan40)
* [doc] Fix doc for dnoise masking_functions.md
* [doc] FIX #297: remove COMMENT
* [doc] Fix links in the how-to (Christophe Courtois )
* [doc] Remove unused links, fix dead ones, fix typos (Gergő Rubint)
* [doc] Deleted an unfinished redundant sentence (Christophe Courtois)
* [doc] success stories
* [doc] Clarify support for Windows binaries

20220517 : 1.0.0 - Production Ready
-------------------------------------------------------------------------------

<!-- https://gitlab.com/dalibo/postgresql_anonymizer/-/milestones/18 -->
<!-- git log --oneline $(git describe --tags --abbrev=0 @^)..@ -->


__Dependencies:__

- pg_crypto

__Changes:__

* No changes :)


20220331 : 0.12.0 - Release Candidate 2
-------------------------------------------------------------------------------

<!-- https://gitlab.com/dalibo/postgresql_anonymizer/-/milestones/17 -->
<!-- git log --oneline $(git describe --tags --abbrev=0 @^)..@ -->

__Dependencies:__

- pg_crypto

__Changes:__

* [doc] Academic research bibliography
* [doc] update the README
* [pg_dump_anon] support --table and --schema in the golang wrapper
* [CI] disable PG 9.6 tests
* [core] FIX #290 improve the permissions system
* [CI] FIX #273 check database owner's permissions
* [core] FIX #165 Database owner cannot create table
* [core] FIX #171 pg_dump_anon: lack of permissions to work with "anon"
* [core] FIX #290 permission denied for schema anon after running some DDL
* [core] New GUC parameters : maskschema and sourcechema
* [docker] bump to PG14o
* [CI] improve windows job

20220331 : 0.11.0 - Release Candidate 1
-------------------------------------------------------------------------------

<!-- https://gitlab.com/dalibo/postgresql_anonymizer/-/milestones/16 -->
<!-- git log --oneline $(git describe --tags --abbrev=0 @^)..@ -->

__Dependencies:__

- pg_crypto

__Changes:__

* [core] Remove obsolete functions
* [dump] FIX #282 pg_dump_anon.sh step 2 is broken
* [dump] Regression on -U option
* [core] declaring masking rules with COMMENT is now deprecated
* [core] Add parallel safety for each function
* [dump] FIX #272: pg_dump_anon throws a stdout error when used with sudo
* [doc] Quick notes about perfomance
* [dump] FIX #281 - Regression on pg_dump_anon -U
* [docker] fix the entrypoint env vars
* [dump] add --data-only option
* [pseudo] Allow any type of seeds for the pseudonymizing functions
* [doc] remove warnings
* [how-to] improvements


20220314 : 0.10.0 - An improved engine and a brand new tutorial
-------------------------------------------------------------------------------

<!-- https://gitlab.com/dalibo/postgresql_anonymizer/-/milestones/15 -->
<!-- git log --oneline $(git describe --tags --abbrev=0 @^)..@ -->

__Dependencies:__

- pg_crypto

__Changes:__

* [noise] anon.noise my fail with an error 22003
* [core] support CASE in masking rules
* [doc] How To
* [core] Remove depency to tsm_system_rows
* [doc] Multiple documentation improvements
* [core] Add support for PostgreSQL 15
* [dump] Many pg_dump_anon improvements
* [pg_dump_anon] FIX #213 dump the sequences values (@ybycode))
* [core] Simplify the main masking event trigger
* [doc] Warning about backups consistency
* [tests] noise tests can hit extreme values
* [noise] Fix numeric_value_out_of_range and datetime_field_overflow errors (@sebastien-helbert)
* [docker] add postgresql faker
* [pseudo] FIX #254: masked roles can use pseudo functions
* [doc] missing URL for source install (@Krysztophe)
* [doc] escaping string + value expression
* [doc] additional details about dynamic masking (thanks to @vitobotta)
* [doc] MD5 signatures values have a uniform distribution
* [doc] Update to the Adding Noise section (@Cristiano)
* [docs] How-To
* [Windows] Fix the register process
* [pg_dump_anon] Introduce a Golang port of pg_dump_anon (beta)
* [doc] Update links.md : typo (@SALES)
* [core] Review the plpgsql code (@fyhuel)
* [doc] install from source with multiple versions
* [pg_dump_anon] Support for --encoding
* [dod] Fix typos (Justin Wei)
* [core] use polymorphism when possible plpgsql (@fyhuel)
* [core] some STABLE functions were incorrectly declared as IMMUTABLE (@fyhuel)
* [core] anon.sql: fix UPSERTS (@fyhuel)
* [doc] Added extra lorem ipsum example (Jakob Serlier)
* [docker] FIX #241: Load the extension at the session level
* [doc] Removed typo (Jakob Serlier)


20210702 : 0.9.0 - Support For PostgreSQL 14
-------------------------------------------------------------------------------

__Dependencies:__

- pg_crypto

__Changes:__

* [core] Restrict masking filters to trusted schemas
* [faking] Add a script to populate the fake data tables
* [doc] Improve documentation wording
* [core] Add support for PostgreSQL 14
* [core] Drop support for PostgreSQL 9.5
* [standalone] Drop support for the standalone version
* [standalone] Drop support for Amazon RDS
* [faking] FIX #223 : bad math in the random generator (Carlos Medeiros)
* [faking] improve performances
* [faking] Add deprecation warning that the `random_*` functions will be removed
  in a future version
* [CI] Add Static application security testing (SAST)



20210210 : 0.8.1 - bugfix release
-------------------------------------------------------------------------------

__Dependencies:__

- tms_system_rows
- pg_crypto

__Changes:__

* [standalone] FIX #219 : Update sequences after fake data is loaded


20210208 : 0.8.0 - Support for Foreign Tables and Partition
-------------------------------------------------------------------------------

<!-- https://gitlab.com/dalibo/postgresql_anonymizer/-/milestones/11 -->

__Dependencies:__

- tms_system_rows
- pg_crypto

__Changes:__

* [doc] FIX #168: how to alter a masked column (Rodrigo Otsuka)
* [doc] FIX #174: How to anonymize 2 columns simultaneously (Nicolas Peltier)
* [rules] FIX #181: handle all chars in MASKED WITH VALUES (Matthieu Larcher)
* [in-place] Refactor anonymize_database to improve perfs  (Sébastien Helbert)
* [core] Add support of partitioned tables (Dmitry Fomin)
* [core] Add support of foreign tables (Paul Bonaud)
* [core] Add schemaname in `pg_masking_rules`
* [doc] Explain the permission model
* [docker] simplify the build process for different PG major versions
* [core] FIX #198: bug in the `shuffle` mecanism
* [doc] Documentation Improvements (Rushal Verma)
* [core] Improve the random generator, deprecated use of `tms_system_rows`



20200928 : 0.7.1 - bugfix release
-------------------------------------------------------------------------------

__Dependencies:__

- tms_system_rows
- pg_crypto

__Changes:__

* [pgxn] fixup META.json

20200925 : 0.7.0 - Generic Hashing and Advanced Faking
-------------------------------------------------------------------------------

__Dependencies:__

- tms_system_rows
- pg_crypto

__Changes:__

* [install] Add a notice to users when they try to load the extension twice
* [CI] Improve the masking test
* [install] Support for PostgreSQL 13
* [noise] add on-the-fly noise functions (Gunnar Nick Bluth)
* [dump] add a hint if a particular table dump fails (Gunnar Nick Bluth)
* [install] FIX #128: add version function (Yann Robin)
* [doc] Security: explain noise reduction attacks
* [doc] How To mask a JSONB column (Fabien Barbier)
* [doc] improve load doc
* [CI] Test install on Ubuntu Bionic
* [doc] DBaaS providers support for EVENT TRIGGERS and dynamic masking (Martin Kubrak)
* [install] Remove dependency to the ddlx extension
* [install] FIX #123: bug in the standalone install script (Florian Desbois)
* [doc] lint markdown
* [hashing] Introducing generic hashing function (Gunnar Nick Bluth)
* [hashing] Storing the hashing salt in a secret table
* [hashing] Add dependency to the pg_crypto extension
* [init] Rename anon.load() to anon.init() for clarity
* [random] new masking function: `anon.random_in(ARRAY['yes','no','maybe'])`
* [in-place] defer all deferrable constraints
* [doc] how to dump roles when using the black box method
* [dump] FIX #146: export sequences data  (Joe Auty)
* [doc] `anon.shuffle()` is not a masking function
* [dump] FIX #129: `--file` option not working (Yann Robin)
* [dump] use arrays for argument lists
* [dump] use shellcheck
* [docker] automatic publication of the `latest` tag
* [masking] FIX #141 `anon.stop_dynamic_masking()` does not remove the mask schema
* [init] fix `anon.reset()`
* [init] FIX #103: Create extension encoding issue (Dattatray Phadtare)
* [init] improve error handling
* [init] add the oid into the CSV tables
* [init] Initcap on table `first_name`
* [doc] Add a troubleshooting guide
* [doc] Typo (Peter Neave)
* [doc] Choose between stable and latest
* [blackbox] FIX #156 stdout permissions (Ilya Gorbunov)
* [init] better error handling
* [init] rename anon.load() to anon.init()
* [doc] how to use the PostgreSQL Faker extension
* [dump] Ignore .psqlrc (Nikolay Samokhvalov)



20200305 : 0.6.0 - Pseudonymization and Improved anonymous dumps
-------------------------------------------------------------------------------

__Dependencies:__

- tms_system_rows
- ddlx

__Changes:__

* [doc] Typos, grammar (Nikolay Samokhvalov)
* [doc] make help
* [security] declare explicitly all function as `SECURITY INVOKER`
* [doc] typos (Sebastien Delobel)
* [docker] improve the "black box" method (Sam Buckingham)
* [dump] Fix #112 : invalid command \."
* [install] use session_preload_libs instead of shared_preload_libs (Olleg Samoylov)
* [anonymize] FIX #114 : bug in anonymize_table() (Joe Auty)
* [bug] Fix syntax error when schema in not in search_path (Olleg Samoylov)
* [doc] Use ISO 8601 for dates (Olleg Samoylov)
* [dump] anon.dump() is not deprecated
* [dump] introducing `pg_dump_anon` command line tool
* [pseudo] introducing pseudonymization functions
* [doc] clean up, typos and reorg
* [detection] introducing the identifiers detection function
* [dump] Allow only partial database dump - Or ignoring specific tables



20191106 : 0.5.0 - Generalization and k-anonymity
-------------------------------------------------------------------------------

__Dependencies:__

- tms_system_rows
- ddlx

__Changes:__

* Introduce the Generalization method with 6 functions that transforms dates
  and numeric values into ranges of value.

* Introduce a k-anonymity assessment function.

* [faking] Add `anon.lorem_ipsum()` to generate classic lorem ipsum texts
* [destruction] New syntax `MASKED WITH VALUE ...`
* [doc] Install on Ubuntu 18.04 (many thanks to Jan Birk )
* [doc] Install with docker
* FIX #93 : Better install documentation
* FIX #95 : Building on FreeBSD/MacOS (many thanks to Travis Miller)



20191018 : 0.4.1 - bugfix release
-------------------------------------------------------------------------------

__Dependencies:__

- tms_system_rows
- ddlx

__Changes:__

* FIX #87 : anon.config loaded twice with pg_restore (Olleg Samoylov)
* [doc] : install with yum

20191014 : 0.4 - Declare Masking Rules With Security Labels
-------------------------------------------------------------------------------

__Dependencies:__

- tms_system_rows
- ddlx

__Changes:__

* Use Security Labels instead of COMMENTs. COMMENTs are still supported

* Automatic Type Casting

* Improve documentation



20190826 : 0.3 - In-place Anonymization and Anonymous dumps
-------------------------------------------------------------------------------

__Dependencies:__

- tms_system_rows
- ddlx

__Changes:__

* In-place Anonymization : Permanently remove sensitive data
  with `anonymize_database()`, `anonymize_table()` or
  `anonymize_column()`.

* Anonymous dumps : Export the entire anonymized database with
  the new `dump()` function. For instance:

  ```console
  psql -q -t -A -c 'SELECT anon.dump()' the_database
  ```

* Dynamic Masking : new functions `start_dynamic_masking()` and
  `stop_dynamic_masking()`

* shuffle an entire column with the new function :

  ```sql
  SELECT anon.shuffle_column('employees','salary', 'id');
  ```

* Add +/-33% of noise to a column with:

  ```sql
  SELECT anon.numeric_noise_on_column('employee','salary',0.33);
  ```

* Add +/-10 years of noise to a date with :

  ```sql
  SELECT anon.datetime_noise_on_column('employee','birthday','10 years');
  ```

* Renamed faking functions for clarity

* FIX #43 : Using unlogged tables was a bad idea

* FIX #51 : tests & doc about explicit casting

* Add `autoload` parameter to `mask_init` function.
  Default to TRUE for backward compatibility

* Add `anon.no_extension.sql` for people in the cloud

* [masking] Improve security tests


20181029 : 0.2 - Dynamic masking and partial functions
-------------------------------------------------------------------------------

### Declare masking rules within the DDL

* Declare a masked column with:

  ```sql
  COMMENT ON COLUMN people.name IS 'MASKED WITH FUNCTION anon.random_last_name()';
  ```

* Declare a masked role with :

  ```sql
  COMMENT ON ROLE untrusted_user IS 'MASKED';
  ```

### New functions for partial scrambling

* `partial()` will partially hide any TEXT value
* `partial_email()` will partially hide an email address


Checkout `demo/partial.sql` and `demo/masking.sql` for more details


20180918 : 0.1.1 - Load a custom dataset
-------------------------------------------------------------------------------

* [doc] How To Contribute
* Add tsm_system_rows in `requires` clause
* Allow loading à custom dataset
* use UNLOGGED tables to speed extension loading


20180831 : 0.0.3 - PGXN Fixup
-------------------------------------------------------------------------------

* FIX #12 : bad package version

20180827 : 0.0.2 - Minor bug
-------------------------------------------------------------------------------

* FIX #11 : install error

20180801 : 0.0.1 - Proof of Concept
-------------------------------------------------------------------------------

* `random_date()` and `random_date_between()``
* `random_string()`
* `random_zip()`
* `random_company()`, `random_siret()`, `random_iban()`
* `random_first_name()`, `random_last_name()`
* Docker file for CI
* tests
* PGXN package
