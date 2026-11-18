CHANGELOG
===============================================================================

<!-- git log --oneline $(git describe --tags --abbrev=0 @^)..@ -->

20260629 : 3.1.3 - Missing ARM builds
-------------------------------------------------------------------------------

* [CI] Re-run the ARM build jobs
* [meta] update the release process


20260629 : 3.1.2 - Security Release
-------------------------------------------------------------------------------

* Make `hash(TEXT)` and `digest(TEXT, TEXT, TEXT)` `RESTRICTED`
  (thanks to Sarath Kumar)
* [CI] Introduce cargo audit
* [doc] use rumdl instead of mdl
* [CI] upgrade to rustc 1.96
* [doc] overview of similar tools
* [core] upgrade dependencies

This release brings a fix for a potential brute-force attack on the hashing
functions (CVE-2026-13455). All users should upgrade as soon as possible.
See [issue 649] for more details.

[issue 649]: https://gitlab.com/dalibo/postgresql_anonymizer/-/work_items/649

If a quick upgrade is not possible, the threat can be blocked with:

    SECURITY LABEL FOR anon ON FUNCTION anon.digest(TEXT, TEXT, TEXT)
    IS 'RESTRICTED';

    SECURITY LABEL FOR anon ON FUNCTION anon.hash(TEXT)
    IS 'RESTRICTED';


20260527 : 3.1.1 - Security Release
-------------------------------------------------------------------------------

* [impexp] remove conditional randomization (thanks to Mehmet Ince)
* [CI] Adding CSV files on ARM packages
* [lint] exclude_rule 'MD013'
* [meta] uv is now strongly recommended

This release brings a fix for a SQL Injection threat (CVE-2026-11945). All
users should upgrade as soon as possible. See [issue 643] for more details.

[issue 643]: https://gitlab.com/dalibo/postgresql_anonymizer/-/work_items/643



20260527 : 3.1.0 - Local Differential Privacy
-------------------------------------------------------------------------------

* [k-anonymity] sanitize column names (see CVE-2026-9617) (thanks to Buut)
* [noise] properly sanitize anon.add_noise_on_datetime_column
* [core] Add a policy param to remove_all_masking_rules
* [dump] Support weird column names (thanks to Zsolt2)
* [docker] Bump to pg18
* [docker] Fix bad version (thanks to MonsieurTain)
* [dp] Add LDP GRRM integration for Local Differential Privacy (Adem Bencheikh Lehocine)
* [random] Add a function to generate a random Point in a Box (benoit lobreau)
* [noise] Add GPS coordinate anonymisation functions (benoit lobreau)
* [core] Massive amount of clippy fixes
* [core] Upgrade to Rust 2024
* [core] Upgrade from pgrx 0.16.1 to 0.18.0


20260220 : 3.0.13 - Simplified Continuous Delivery
-------------------------------------------------------------------------------

( tags from 3.0.6 to 3.0.12 were used for CI testing and are skipped )

* [CD] Simplify the deploy task

20260217 : 3.0.5 - Build on ARM64
-------------------------------------------------------------------------------

( tags 3.0.2, 3.0.3, 3.0.4 were used for CI testing and are skipped )

* [CI] Build on ARM

20260204 : 3.0.1 - Parallel Static Masking + JSON Import/Export
-------------------------------------------------------------------------------

* [core] Add functions to import and export to and from a nested json format
* [docs] replica masking is GA
* [replica] Remove triggers when a masking is removed
* [replica] Make labels more random in replica masking trigger functions
* [docs] available on...
* [docs] tips are not displayed (Robin Portigliatti)
* [docs] typo in the tutorial (Ludovic GILBON)
* [docs] fixup ON UPDATE CASCADE (EFLUID)
* [static] Parallel Static Masking (Pierre-Marie Petit)
* [core] Adds 4 catalog views
* [dynamic] Remove Legacy Dynamic Masking
* [docs] Foreign Keys
* [meta] Guidelines for AI content
* [packaging] Build and Publish OCI image
* [static] always defer all constraints
* [static] Improve selective masking (EFLUID)
* [docs] Links to Matrix and Discord
* [core] Custom Values (EFLUID)
* [core] Drop PostgreSQL 13
* [sampling] remove unused function ( see CVE-2026-2361 )
* [core] Protect operators against uncontrolled search_path elements
  ( see CVE-2026-2360 )


20251118 : 2.5.1 - Minor bug fixes
-------------------------------------------------------------------------------

* [static] FIX #589 : correctly handle policy in anonymize_database
* [doc] update the release process

20251117 : 2.5.0 - Maintenance release
-------------------------------------------------------------------------------

* [core] Refactor rule parsing functions
* [backup masking] Remove pg_dump_anon ( was deprecated since version 2.0)
* [pseudo] FIX #582 - allow special characters in database name
* [static] FIX #581 - mask tables with only tablesample rules
* Document how to anonymize with a ratio of NULL values (Benoit Lobréau)
* Reset the search_path when anonymizing an SQL file (Hans Hübner)

20250919 : 2.4.1 - Minor bug fixes
-------------------------------------------------------------------------------

* [static masking] Remove search_path spec in anonymize_database()
* Fix #576 build on other architectures than x86_64 (pkhartsk)

20250911 : 2.4.0 - PostgreSQL 18 (beta)
-------------------------------------------------------------------------------

* [core] Introduce Selective Masking (Beta)
* [core] FIX #563 anon.replace binding incorrect (Alex Akeno)
* [meta] remove binary .DS_Store files and add them to gitignore (pkhartsk)
* [core] refactor tupleDescData loops
* [core] Support PostgreSQL 18 (beta)
* [lint] run cargo clippy
* [core] upgrade to PGRX 0.16.0
* [core] Hooks refactoring
* [core] GUC refactoring
* [tests] Fix parse_select_query on PG15+
* [pseudo] Introduce new shift/xor functions
* [pseudo] Prevent masked users from brute forcing the pseudonymizing functions
* [doc] Install from source: fixed typo in PGVER variable (pkhartsk)
* [tdm] FIX #553 - Respect the FROM ONLY clause for masked users
  (thanks to MonsieurTain)
* [doc] Add missing semicolon in quickstart (Marcus Olsson)
* [doc] Corrects typo (mardub)

20250702 : 2.3.0 - Parallel Static Masking
-------------------------------------------------------------------------------

* [docs] Various corrections on the tutorial (Robin Portigliatti)
* [random] FIX #527 issue with negative numbers
* [make] build on Mac OS X (Suhas Thalanki)
* [doc] How to truncate a table for masked users
* [replica] Introducing Replica Masking (ALPHA)
* [tests] make ldm test more robust
* [doc] hash and unescaped chars

20250530 : 2.2.1 - packaging fixup
-------------------------------------------------------------------------------

* [CI] deploy packages on each tags

20250528 : 2.2.0 - Masking Cursors
-------------------------------------------------------------------------------

* [core] Add anon.static_masking GUC param (Daniel Solsona)
* [core] Masking cursors (Jukka Heiskanen)
* [doc] typos and malformed syntaxes (Julien Acroute)
* [core] bump to pgrx 0.14.3
* [doc] Fix the edit on gitlab link
* [doc] Link to the official Neon documentation (Philip Olson)
* [pgxn] Drop publishing on PGXN for now


20250418 : 2.1.1 - packaging fixup
-------------------------------------------------------------------------------

* Fixing a minor issue with packaging

20250418 : 2.1.0 - Masking Images
-------------------------------------------------------------------------------

* [image] add blur image functions (pmpetit)
* [doc] Install With PGXN is not possible currently
* [doc] Remove Privacy By Default disclaimer
* [doc] sha1 hashing is not available in v2 (Anthony DUMONTOIS)
* [doc] Install with Ansible
* [doc] Fix Anonymous Dump schema
* [doc] Fix forgotten k_anonymity provider in docs
* [doc] Typos and formatting (pmpetit)
* [doc] Available on Alibabacloud
* [doc] Uninstall best practices
* [doc] Fix typo in docs/configure.md (Matthias van de Meent)
* [install] Fix make install (@Krysztophe)
* [make] Start the instance before installcheck
* [core] Bump toPGRX 0.14
* [faker] Bump to fake 4.3
* [faker] Remove useless type `anon_fake_data_tables`
* [generalization] test with timezone to UTC to avoid LMT/PST failures
* [core] Activate rustfmt

20250106 : 2.0.0 - Better, Faster, Safer
-------------------------------------------------------------------------------

* [doc] sampling with RLS
* [doc] clarify deprecation note for pg_dump_anon (pmpetit)
* [doc] explain a little more what is deprecated (pmpetit)
* [tutorial] clarify the anonymous dump tutorial (pmpetit)
* [doc] fix pg_dump_anon deprecated message, explain that user can use their
  own exporter role to do the job (pmpetit)
* [tutorial] wrong table name (pmpetit)
* [doc] add tips about dummy and fake functions (pmpetit)

20241218 : 2.0.0-rc.3 - Better, Faster, Safer
-------------------------------------------------------------------------------

* [doc] Improve examples
* [make] reorder test files
* [static] masking a quoted column
* [tests] Add more unit tests
* [random] remove random_date_after and random_date_before


20241210 : 2.0.0-rc.2 - Better, Faster, Safer
-------------------------------------------------------------------------------

* [random] remove useless warning
* [core] FIX #493 - Support CHAR(n) type in transparent dynamic masking
  (Thanks to Ben Dempsey)
* [tests] fix the dropped_columns test for PG15+
* [tests] add tests on schema & column (Pierre-Marie Petit)
* add gitignore for vscode (Pierre-Marie Petit)
* [doc] Fix Typo (Pierre-Marie Petit)
* [core] Fix #492: Error when a table has a dropped column
  (Thanks to Danilo Lourenço Costa Oliveira)
* [tests] Add more unit tests

20241202 : 2.0.0-rc.1 - Better, Faster, Safer
-------------------------------------------------------------------------------

* Update years
* [V1] Remove deprecated bash tests
* [V1] Remove useless git modules config
* [V1] Remove the shell code
* [V1] remove ancient debian folder
* [V1] Remove Python code
* [V1] Remove C code
* Remove useless files
* [CI] move the job templates
* [doc] update and merge the release process

20241125 : 2.0.0-beta.4 - Better, Faster, Safer
-------------------------------------------------------------------------------

* Bump to PGRX 0.12.9
* [core] restrict debug output to debug mode
* [dump] use --exclude-extension
* FIX #485 notice_if_not_init (LeoLong)
* [tests] Minor fix in the COPY test
* [core] ignore masking rules in the `anon` schema (Konrad Kucharski)
* [core] fix minor warnings
* [test] FIX minor problem in the test for privacy by default
* [doc] Add tutorials
* [doc] NEWS 2.0
* [doc] Update README
* [static] Support for identity columns
* Masking Foreign Data Wrapper
* Fix (unofficial) support for ARM64
* Bump to 2.0.0-beta.4
* [core] Drop Support for PostgreSQL 12
* [core] FIX #479 Allow privacy by default for anonymous dumps (Konrad Kucharski)
* [random] Generate a random but unique value with anon.random_id

20241023 : 2.0.0-beta.3 - Better, Faster, Safer
-------------------------------------------------------------------------------

* [tests] Masking a view is supported
* [tests] RLS policies are supported
* [core] Upgrade to PGRX 0.12.6
* [CI] Enable test coverage
* [docker] Add code coverage tools to the PGRX image
* [CI] track the lock file
* [core] Support generated columns
* [doc] Generalization sections become 'Masking Views'
* [generalization] Fix timezone for the tests
* [doc] Advanced Faking
* [black_box] Using black box with pg_dump parameters
* [pg_dump_anon] remove bats submodules
* [CI] fix deb smoketest

20240925 : 2.0.0-beta.2 - Better, Faster, Safer
-------------------------------------------------------------------------------

* Bump to fake-rs 2.10
* [rpm] [deb] Improve packaging
* [CI] remove cache
* [docker] PGRX image is now based on Rocky 8
* [docs] allow long lines in markdown tables
* [docs] Update deb/rpm Install methods
* [tests] Manual smoke tests to check the install process on various platforms



20240918 : 2.0.0-beta.1 - Better, Faster, Safer
-------------------------------------------------------------------------------

* Revert Rust random date functions
* Upgrade to PGRX 0.12.4
* Add support for PostgreSQL 17
* [tests] better error handling in the privacy_by_default test
* [walker] support volatile functions with transparent dynamic masking
* FIX #452 : use volatile functions in TDM
* [doc] improve INSTALL procedure (Giampaolo Capelli)
* [make] Add extension target
* Use static regexp
* Rewrite the static masking functions in Rust
* Multiple Masking Policies
* [core] Use TreeWalker for checking inputs
* [doc] Database-level settings are visible only in future sessions
* [dynamic] Disable permission check for the masking subquery
* [random] random_string(0) must return an empty string (José Pedro Saraiva)
* RawParseMode_RAW_PARSE_DEFAULT is deprecated
* Build on ARM64 (José Pedro Saraiva)
* Transparent Dynamic Masking


20240704 : 2.0.0-alpha.2 - Better, Faster, Safer
-------------------------------------------------------------------------------

* [CI] Activate Rust cache
* [CI] Add workflow rules to prevent double pipelines
* [CI] Activate clippy
* [core] Trust more pg_catalog functions (thanks to @nocive)
* [core] Improve management and errors for TRUSTED functions
* [CI] add a release job for DEB/RPM packages
* [doc] How to add tests
* [docker] use PostgreSQL version 16 by default in the Dockerfile (@madtibo)
* [core] Split test fixtures and extern functions into modules
* [tests] Add elevation tests
* [core] split the hooks in a separate file
* [CI] fix how-to generation
* [doc] build the doc of a previous version
* [TDM] issue error on `COPY (SELECT ...) TO ...` statements
* [meta] Add codespell and markdownlint to pre-commit
* [faker] New fake data generator
* [doc] new install methods
* [doc] Officially drop Windows support


20240408 : 2.0.0-alpha.1 - Better, Faster, Safer
-------------------------------------------------------------------------------

* [core] Rewrite the core library in Rust


20240321 : 1.3.2 - Fix pg_catalog bindings
-------------------------------------------------------------------------------

__Changes:__

* [bindings] Add date functions
* [doc] Now available on Neon
* [bindings] Add replace function
* [CI] release a tagged docker image for each COMMIT_TAG (Guillaume Risbourg)
* [bindings] Fix multiple signature errors in the pg_catalog bindings (Austin Putman)
* [docs] Fix wrong link on detection.md (Carlos Ruiz)
* [bindings] Add anon.length bindings
* [docs] fix "Black Box" shortcut command in INSTALL.md (Bojan Mihelac)


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
* [data] update the dictionary of english identifiers
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
* [doc] Quick notes about performance
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
* [core] Remove dependency to tsm_system_rows
* [doc] Multiple documentation improvements
* [core] Add support for PostgreSQL 15
* [dump] Many pg_dump_anon improvements
* [pg_dump_anon] FIX #213 dump the sequences values (@ybycode))
* [core] Simplify the main masking event trigger
* [doc] Warning about backups consistency
* [tests] noise tests can hit extreme values
* [noise] Fix numeric_value_out_of_range and datetime_field_overflow errors
  (@sebastien-helbert)
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
* [in-place] Refactor anonymize_database to improve perfs (Sébastien Helbert)
* [core] Add support of partitioned tables (Dmitry Fomin)
* [core] Add support of foreign tables (Paul Bonaud)
* [core] Add schemaname in `pg_masking_rules`
* [doc] Explain the permission model
* [docker] simplify the build process for different PG major versions
* [core] FIX #198: bug in the `shuffle` mechanism
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
* [dump] FIX #146: export sequences data (Joe Auty)
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
