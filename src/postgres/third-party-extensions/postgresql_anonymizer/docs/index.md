![PostgreSQL Anonymizer](https://gitlab.com/dalibo/postgresql_anonymizer/raw/master/images/png_RVB/PostgreSQL-Anonymizer_H_couleur.png)


Anonymization & Data Masking for Postgres
===============================================================================

`PostgreSQL Anonymizer` is an extension to mask or replace
[personally identifiable information] (PII) or commercially sensitive data from
a Postgres database.

The project has a **declarative approach** of anonymization. This means you can
[declare the masking rules] using the PostgreSQL Data Definition Language (DDL)
and specify your anonymization policy inside the table definition itself.

The main goal of this extension is to offer **anonymization by design**. We
firmly believe that data masking rules should be written by the people who develop
the application because they have the best knowledge of how the data model works.
Therefore masking rules must be implemented directly inside the database schema.

Once the masking rules are defined, you can apply them using 6 different
**masking methods** :

* [Anonymous Dumps]: Simply export the masked data into an SQL file
* [Static Masking]: Remove the PII according to the rules
* [Dynamic Masking]: Hide PII only for the masked users
* [Replica Masking]: Create an anonymized clone of a database
* [Masking Views]: Build dedicated views for the masked users
* [Masking Data Wrappers]: Apply masking rules on external data

Each method has its pros and cons. Different masking methods may be used in
different contexts. In any case, masking the data directly inside the PostgreSQL
instance without using an external tool is crucial to limit the exposure and
the risks of data leak.

In addition, various [Masking Functions] are available : randomization, faking,
partial scrambling, shuffling, noise, generalization or even your own custom function!

Finally, the extension offers a panel of [detection] functions that will try to
guess which columns need to be anonymized.

[personally identifiable information]: https://en.wikipedia.org/wiki/Personally_identifiable_information
[declare the masking rules]: https://postgresql-anonymizer.readthedocs.io/en/stable/declare_masking_rules/

[Anonymous Dumps]: https://postgresql-anonymizer.readthedocs.io/en/stable/anonymous_dumps/
[Static Masking]: https://postgresql-anonymizer.readthedocs.io/en/stable/static_masking/
[Dynamic Masking]: https://postgresql-anonymizer.readthedocs.io/en/stable/dynamic_masking/
[Replica Masking]: https://postgresql-anonymizer.readthedocs.io/en/stable/replica_masking/
[Masking Functions]: https://postgresql-anonymizer.readthedocs.io/en/stable/masking_functions/
[Masking Views]: https://postgresql-anonymizer.readthedocs.io/en/stable/masking_views/
[Masking Data Wrappers]: https://postgresql-anonymizer.readthedocs.io/en/stable/masking_data_wrappers/
[detection]: https://postgresql-anonymizer.readthedocs.io/en/stable/detection/

Available on ...
------------------------------------------------------------------------------

PostgreSQL Anonymizer is available on multiple linux distributions,
cloud service providers, development frameworks and PostgreSQL forks:

<!-- markdownlint-disable MD033 -->
<div class="grid">

<a href="#" class="card"><img src="images/logos/aiven.png"></a>
<a href="#" class="card"><img src="images/logos/alibaba.png"></a>
<a href="#" class="card"><img src="images/logos/ansible.png"></a>
<a href="#" class="card"><img src="images/logos/azure.png"></a>
<a href="#" class="card"><img src="images/logos/crunchy.png"></a>
<a href="#" class="card"><img src="images/logos/debian.png"></a>
<a href="#" class="card"><img src="images/logos/django.png"></a>
<a href="#" class="card"><img src="images/logos/docker.png"></a>
<a href="#" class="card"><img src="images/logos/edb.png"></a>
<a href="#" class="card"><img src="images/logos/google.png"></a>
<a href="#" class="card"><img src="images/logos/greenplum.png"></a>
<a href="#" class="card"><img src="images/logos/ibm.png"></a>
<a href="#" class="card"><img src="images/logos/neon.png"></a>
<a href="#" class="card"><img src="images/logos/postgrespro.png"></a>
<a href="#" class="card"><img src="images/logos/redhat.png"></a>
<a href="#" class="card"><img src="images/logos/rocky.png"></a>
<a href="#" class="card"><img src="images/logos/suse.png"></a>
<a href="#" class="card"><img src="images/logos/ubuntu.png"></a>
<a href="#" class="card"><img src="images/logos/yandex.png"></a>
<a href="#" class="card"><img src="images/logos/yugabyte.png"></a>

</div>


See the [INSTALL](INSTALL) section for more details.

Quick Start
------------------------------------------------------------------------------

Step 0. Launch docker image of the project

``` console
ANON_IMG=registry.gitlab.com/dalibo/postgresql_anonymizer
docker run --name anon_quickstart --detach -e POSTGRES_PASSWORD=x $ANON_IMG
docker exec -it anon_quickstart psql -U postgres
```

Step 1. Create a database and load the extension in it

``` sql
CREATE DATABASE demo;
ALTER DATABASE demo SET session_preload_libraries = 'anon';

\connect demo
You are now connected to database "demo" as user "postgres".
```

Step 2. Create a table

```sql
CREATE TABLE people AS
    SELECT  153478       AS id,
            'Sarah'      AS firstname,
            'Conor'      AS lastname,
            '0609110911' AS phone
;
```

```sql
SELECT * FROM people;
   id   | firstname | lastname |   phone
--------+-----------+----------+------------
 153478 | Sarah     | Conor    | 0609110911
```

Step 3. Create the extension and activate the masking engine

```sql
CREATE EXTENSION anon;
ALTER DATABASE demo SET anon.transparent_dynamic_masking TO true;
```

Step 4. Declare a masked user

```sql
CREATE ROLE skynet LOGIN;

SECURITY LABEL FOR anon ON ROLE skynet IS 'MASKED';

GRANT pg_read_all_data to skynet;
```

Step 5. Declare the masking rules

```sql
SECURITY LABEL FOR anon ON COLUMN people.lastname
  IS 'MASKED WITH FUNCTION anon.dummy_last_name()';

SECURITY LABEL FOR anon ON COLUMN people.phone
  IS 'MASKED WITH FUNCTION anon.partial(phone,2,$$******$$,2)';
```

Step 6. Connect with the masked user

```sql
\connect - skynet
You are now connected to database "demo" as user "skynet"

SELECT * FROM people;
   id   | firstname | lastname  |   phone
--------+-----------+-----------+------------
 153478 | Sarah     | Stranahan | 06******11
```


Success Stories
------------------------------------------------------------------------------

> With PostgreSQL Anonymizer we integrate, from the design of the database,
> the principle that outside production the data must be anonymized. Thus we
> can reinforce the GDPR rules, without affecting the quality of the tests
> during version upgrades for example.

— **Thierry Aimé, Office of Architecture and Standards in the French
Public Finances Directorate General (DGFiP)**

---

> Thanks to PostgreSQL Anonymizer we were able to define complex masking rules
> in order to implement full pseudonymization of our databases without losing
> functionality. Testing on realistic data while guaranteeing the
> confidentiality of patient data is a key point to improve the robustness of
> our functionalities and the quality of our customer service.

— **Julien Biaggi, Product Owner at bioMérieux**

---

> I just discovered your postgresql_anonymizer extension and used it at
> my company for anonymizing our user for local development. Nice work!

— **Max Metcalfe**

If this extension is useful to you, please let us know !

Support
------------------------------------------------------------------------------

We need your feedback and ideas ! Let us know what you think of this tool, how
it fits your needs and what features are missing.

Want to talk directly with us ? Join us on Matrix or Discord

* Matrix: https://matrix.to/#/#anon:dalibo.com
* Discord: https://discord.com/channels/710918545906597938/1427672533104070807

You can also [open a public issue] on gitlab

If you need enterprise support or if you want to sponsor the project, send us
a message at <contact@dalibo.com>.

[open a public issue]: https://gitlab.com/dalibo/postgresql_anonymizer/issues
