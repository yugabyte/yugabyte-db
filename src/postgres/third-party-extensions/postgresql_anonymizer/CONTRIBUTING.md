How To Contribute
===============================================================================

This project is an **open project**. Any comment or idea is more than welcome.

Here's a few tips to get started if you want to get involved

Where to start ?
------------------------------------------------------------------------------

If you want to help, here's a few ideas :

1- **Testing** : You can install the `latest` branch of the project and realize
extensive tests based on your use case. This is very useful to improve the
stability of the code. Eventually if you can publish you test cases, please
add them in the `/tests/sql` directory or in `demo`. I have recently
implemented "anonymous dumps" and I need feedback !

2- **Documentation** : You can write documentation and examples to help new
users. I have created a `docs` folder where you can put documentation on
how to install and use the extension...

3- **Benchmark** : You run tests on various setups and measure the impact of the
extension on performances

4- **Junior Jobs** : I have flagged a few issues as "[Junior Jobs]" on the
project [issue board]. If you want to give a try, simply fork the git repository
and start coding !

5- **Spread the Word** : If you look this extension, just let other people know!
You can publish a blog post about it or a youtube video or whatever format
you feel comfortable with !

In any case, let us know how we can help you moving forward

[Junior Jobs]: https://gitlab.com/dalibo/postgresql_anonymizer/issues?label_name%5B%5D=Junior+Jobs
[issue board]: https://gitlab.com/dalibo/postgresql_anonymizer/issues


Forking, mirroring and Rebasing
-------------------------------------------------------------------------------

To contribute code to this project, you can simply create you own fork.

Over time, the main repository (let's call it `upstream`) will evolve and your
own repository (let's call it `origin`) will miss the latest commits. Here's
a few hints on how to handle this

### Connect your repo to the upstream

Add a new remote to your local repo:

```bash
git remote add upstream https://gitlab.com/dalibo/postgresql_anonymizer.git
```

### Keep your `latest` branch up to date

At any time, you can mirror your personal repo like this:

```bash
# switch to the latest branch
git checkout latest
# download the latest commits from the upstream repo
git fetch upstream
# apply the commits
git rebase upstream/latest
# push the changes to your personal repo
git push origin
```

### Rebase a branch

When working on a Merge Requests (`MR`) that takes a long time, it can happen
that your local branch (let's call it `foo`) is out of sync. Here's how you
can apply the latest:


```bash
# switch to your working branch branch
git checkout foo
# download the latest commit from the main repo
git fetch upstream
# apply the latest commits
git rebase upstream/latest
# push the changes to your personal repo
git push origin --force-with-lease
```

Set up a development environment
-------------------------------------------------------------------------------

This extension is written in SQL, pl/pgsql and Rust. It relies on a Rust
framework named [PGRX].

To set up, your development environment follow the [PGRX install instructions] !

We also highly recommend installing `uv` !

``` console
cargo install uv
cargo install cargo-audit --features=fix
```

Alternatively you use the docker image we built for that, simply by running:

``` console
make pgrx_bash
```

You will be logged in a PGRX environment with the project repo mounted in
the `/pgrx` folder.

> NOTE: If you're not using Docker Desktop and your UID / GID is not 1000,
> then you may get permission errors with the `/pgrx` volume.
> You can fix that by rebuilding the image locally with:

``` console
export PGRX_BUILD_ARGS="--build-arg UID=`id -u` --build-arg GID=`id -g`"
make docker_image
# ... /!\ this may take a while ...
make docker_bash
```


[PGRX]: https://github.com/pgcentralfoundation/pgrx
[PGRX install instructions]: https://github.com/pgcentralfoundation/pgrx#system-requirements



Adding new functions
-------------------------------------------------------------------------------

The set of functions is based on pragmatic experience and feedback. We try to
cover the most common personal data types. If you need an additional function,
let us know !

If you want to add new functions, please define the following attributes:

* volatility: should be `VOLATILE` (default), `STABLE` or `IMMUTABLE`
* strict mode: `CALLED ON NULL INPUT`(default) or `RETURNS NULL ON NULL INPUT`
* security level: `SECURITY INVOKER`(default) or `SECURITY DEFINER`
* parallel mode: `PARALLEL UNSAFE` (default) or `PARALLEL SAFE`
* search_path: `SET search_path=''`

Please read the [CREATE FUNCTION] documentation for more details.

[CREATE FUNCTION]: https://www.postgresql.org/docs/current/sql-createfunction.html


In most cases, a masking functions should have the following attributes:

```sql
CREATE OR REPLACE FUNCTION anon.foo(TEXT)
RETURNS TEXT AS
$$
    SELECT ...
$$
    LANGUAGE SQL
    VOLATILE
    RETURNS NULL ON NULL INPUT
    PARALLEL UNSAFE
    SECURITY INVOKER
    SET search_path=''
;
```

Adding new tests
-------------------------------------------------------------------------------

The functional tests are managed with `pg_regress`, a component of the [PGXS]
extension framework. You can simply launch the tests with:

```bash
make
make install
make installcheck
```

Adding a new test is not very intuitive. Here's a quick method to create a
test named `foo`:

1. Write your tests in `tests/sql/foo.sql`
2. Run it with `make installcheck REGRESS=foo`
3. Check the output in `results/foo.out`
4. If the output is not the expected result, then return to step 1
5. Else copy `results/foo.out` in `tests/expected`
6. Open the `Makefile`, add `foo` in the `REGRESS_TESTS` variable
7. Run `make installcheck`


[PGXS]: https://www.postgresql.org/docs/current/extend-pgxs.html


Testing with docker
-------------------------------------------------------------------------------

You can easily set up a proper testing environment from scratch with docker !

First launch a container and log into with:

```bash
make pgrx_bash
```

For manual testing:

```bash
make run
```

To launch the unit tests:

```bash
make test
```

To launch the functional tests:

```bash
make
make install
make installcheck
```

The entire test suite take a few minutes to run. When developing a feature,
usually you only want to check one test in particular. You can limit the scope
of the test run with the `REGRESS` variable.

For instance, if you want to run only the `noise.sql` test:

```bash
make installcheck REGRESS=noise
```

By default the tests are launched against one PostgreSQL major version (as
defined in `Cargo.toml`). To launch the test suite against another version
export the `PGVER` variable:

```bash
export PGVER=pg15
make run
make test
# etc.
```

Debug mode
--------------------------------------------------------------------------------

By default, the extension is built with the Rust `--release` mode.

For a more verbose output, you can enable the debug mode with

``` bash
TARGET=debug make run
```

This will give you access to:

* the extension debug logs produced by the `log::debug1!` and `log::debug3!`
  macros

* Additional SQL functions that provide priceless information when we need to
  fix a bug or develop a new feature, such as
  `SELECT anon.get_masking_policy(OID)`.

In CI, the extension is built with the release mode, which means that the DEB
and RPM packages are also in release mode.



Build the docs
--------------------------------------------------------------------------------

We publish 2 versions of the documentation `stable` and `latest`.

If you want to read the documentation of a previous version, you can simply read
the markdown files in the `docs` folder :

```bash
# replace `1.1.0` with the version you want
git checkout 1.1.0
uvx mkdocs build
cd site
```

Linting
--------------------------------------------------------------------------------

Use `make lint` to run the various linters on the project.

### Git pre-commit hook

We maintain a [pre-commit] configuration to operate some verification at commit
time, if you want to use that configuration you can simply run:

``` bash
uvx prek install
```

This guarantee that any commit you make will respect our basic formatting rules.

Fake Data
--------------------------------------------------------------------------------

By default, the extension is shipped with an english fake dataset.

### Update the fake dataset

``` console
make fake_data
git commit data
```

### Add a new language

To add a new fake dataset in another language, just change the
`FAKE_DATA_LOCALES` variable

``` console
mkdir -p data/fr_FR/fake
FAKE_DATA_LOCALES=fr_FR make fake_data
```


Compatibility with ARM
--------------------------------------------------------------------------------

We do not officially support this extension on ARM64 architectures.

However some people have successfully build the extension for ARM64 and here's
some good practice to maintain compatibility.

On some ARM platforms a "char" is actually an "unsigned integer" (u8) while on
AMD64 it is a signed integer (i8). To avoid compilation errors, we use the
`std::os::raw::c_char` type instead of `i8`, especially for C char pointers.

For example:

``` rust
let belt = belt_cstr.as_ptr() as *const i8;
```

becomes

``` rust
use std::os::raw::c_char;
let belt = belt_cstr.as_ptr() as *const c_char;
```


Security
--------------------------------------------------------------------------------


### About SQL Injection

By design, this extension is prone to SQL Injections risks. When adding new
features, a special focus should be made on security, especially by sanitizing
the functions parameters and using `regclass` and `oid` instead of literal
names to designate objects...

See links below for more details:

* https://stackoverflow.com/questions/10705616/table-name-as-a-postgresql-function-parameter
* https://www.postgresql.org/docs/current/datatype-oid.html
* https://xkcd.com/327/

### Security level for functions

Most functions should be defined as `SECURITY INVOKER`. In very exceptional
cases, it may be necessary to use `SECURITY DEFINER` but this should be used
with care.

Read the [CREATE FUNCTION] documentation for more details:

https://www.postgresql.org/docs/current/sql-createfunction.html#SQL-CREATEFUNCTION-SECURITY

### Search_path

This extension will create views based on masking functions. These functions
will be run as with privileges of the owners of the views. This is prone
to [search_path attacks]: an untrusted user may be able to override some
functions and gain superuser privileges.

Therefore all functions should be defined with `SET search_path=''` even if
they are not `SECURITY DEFINER`.

[search_path attacks]: https://www.cybertec-postgresql.com/en/abusing-security-definer-functions/


Publishing a new Release
--------------------------------------------------------------------------------

See .gitlab/issue_templates/Release.md


Guidelines for Tool Generated Content
--------------------------------------------------------------------------------

Code generation tools such as LLMs are becoming more and more powerful and they
can improve the developers productivity. In the meantime, they can create a
lot of useless content and make us waste a lot of our time in reviewing and
maintenaning those contributions.

In order to establish a balance between productivity and code quality, here's
a few expectations defined to maintain trust between submitters and reviewers.

When submitting a contribution, please be transparent about the origin of
content by adding information like :

* What tools were used?
* The input to the tools you used
* If code was largely generated from a single or short set of
  prompts, include those prompts in the commit description.
* For bigger contributions, include a summary of the prompts
* Which portions of the content were affected by that tool?

While your contribution is reviewed, the reviewers may ask you to :

* Try some alternative prompts
* Provide more details on how the tools or models were trained
* Explain how the code works without using an external tool, to check
  that you have a good understanding of what your contribution does

As always in Open Source development, Transparency is the foundation of Trust.
