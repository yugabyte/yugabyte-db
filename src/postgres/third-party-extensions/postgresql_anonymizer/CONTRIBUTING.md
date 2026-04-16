How To Contribute
===============================================================================

This project is an **open project**. Any comment or idea is more than welcome.

Here's a few tips to get started if you want to get involved

Where to start ?
------------------------------------------------------------------------------

If you want to help, here's a few ideas :

1- **Testing** : You can install the `master` branch of the project and realize
extensive tests based on your use case. This is very useful to improve the
stability of the code. Eventually if you can publish you test cases, please
add them in the `/tests/sql` directory or in `demo`. I have recently
implemented "anonymous dumps" and I need feedback !

2- **Documentation** : You can write documentation and examples to help new
users. I have created a `docs` folder where you can put documentation on
how to install and use the extension...

3- **Benchmark** : You run tests on various setups and measure the impact of the
extension on performances

4- **Junior Jobs** : I have flagged a few issues as "[Junior Jobs]"  on the project
[issue board]. If you want to give a try, simply fork the git repository
and start coding !

5- **Spread the Word** : If you look this extension, just let other people know !
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

### Keep your master branch up to date

At any time, you can mirror your personal repo like this:

```bash
# switch to the master branch
git checkout master
# download the latest commit from the main repo
git fetch upstream
# apply the latest commits
git rebase upstream/master
# push the changes to your personal repo
git push origin
```

### Rebase a branch

When working on a Merge Requests (`MR`) that takes a long time, it can happen
that your local branch (let's call it `foo`) is out of sync. Here's how you
can apply the lastest:


```bash
# switch to your working branch branch
git checkout foo
# download the latest commit from the main repo
git fetch upstream
# apply the latest commits
git rebase upstream/master
# push the changes to your personal repo
git push origin --force-with-lease
```




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

Testing with docker
-------------------------------------------------------------------------------

You can easily set up a proper testing environment from scratch with docker
and docker-compose !

First launch a container with :

```bash
make docker_init
```

Then you can enter inside the container :

```bash
make docker_bash
```

Once inside the container, you can do the classic operations :

```bash
make
make install
make installcheck
psql
```

The entire test suite take a few minutes to run. When developping a feature,
usually you only want to check one test in particular. You can limit the scope
of the test run with the `REGRESS` variable.

For instance, if you want to run only the `noise.sql` test:

```bash
make installcheck REGRESS=noise
```

Linting
--------------------------------------------------------------------------------

Use `make lint` to run the various linters on the project.

### Git pre-commit hook

We maintain a [pre-commit] configuration to operate some verification at commit
time, if you want to use that configuration you should:

- Install pre-commit (On Debian based system you can probably simply run :
  `sudo apt install pre-commit`)
- Then apply the configuration with `pre-commit install`
- And finally you can verify the configuration is properly applied by running
  it "by hand": `.git/hooks/pre-commit`

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

Most functions should be defined as `SECURITY INVOKER`. In very exceptional cases,
it may be necessary to use `SECURITY DEFINER` but this should be used with care.

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
