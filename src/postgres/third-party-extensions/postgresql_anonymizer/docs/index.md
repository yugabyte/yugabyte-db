![PostgreSQL Anonymizer](https://gitlab.com/dalibo/postgresql_anonymizer/raw/master/images/png_RVB/PostgreSQL-Anonymizer_H_couleur.png)


Anonymization & Data Masking for PostgreSQL
===============================================================================

`postgresql_anonymizer` is an extension to mask or replace
[personally identifiable information] (PII) or commercially sensitive data from
a PostgreSQL database.

The project has a **declarative approach** of anonymization. This means you can
[declare the masking rules] using the PostgreSQL Data Definition Language (DDL)
and specify your anonymization strategy inside the table definition itself.

Once the maskings rules are defined, you can access the anonymized data in 3
different ways :

* [Anonymous Dumps] : Simply export the masked data into an SQL file
* [Static Masking] : Remove the PII according to the rules
* [Dynamic Masking] : Hide PII only for the masked users

In addition, various [Masking Functions] are available : randomization, faking,
partial scrambling, shuffling, noise or even your own custom function!

Beyond masking, it is also possible to use a fourth approach called [Generalization]
which is perfect for statistics and data analytics.

Finally, the extension offers a panel of [detection] functions that will try to
guess which columns need to be anonymized.

[personally identifiable information]: https://en.wikipedia.org/wiki/Personally_identifiable_information
[declare the masking rules]: declare_masking_rules.md

[Anonymous Dumps]: anonymous_dumps.md
[Static Masking]: static_masking.md
[Dynamic Masking]: dynamic_masking.md
[Masking Functions]: masking_functions.md
[Generalization]: generalization.md
[detection]: detection.md



Example
------------------------------------------------------------------------------

```sql
=# SELECT * FROM people;
 id | firstname | lastname |   phone
----+-----------+----------+------------
 T1 | Sarah     | Conor    | 0609110911
```

Step 1 : Activate the dynamic masking engine

```sql
=# CREATE EXTENSION IF NOT EXISTS anon CASCADE;
=# SELECT anon.start_dynamic_masking();
```

Step 2 : Declare a masked user

```sql
=# CREATE ROLE skynet LOGIN;
=# SECURITY LABEL FOR anon ON ROLE skynet IS 'MASKED';
```

Step 3 : Declare the masking rules

```sql
=# SECURITY LABEL FOR anon ON COLUMN people.lastname
-# IS 'MASKED WITH FUNCTION anon.fake_last_name()';

=# SECURITY LABEL FOR anon ON COLUMN people.phone
-# IS 'MASKED WITH FUNCTION anon.partial(phone,2,$$******$$,2)';
```

Step 4 : Connect with the masked user

```sql
=# \connect - skynet
=> SELECT * FROM people;
 id | firstname | lastname  |   phone
----+-----------+-----------+------------
 T1 | Sarah     | Stranahan | 06******11
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

You can either [open an issue] or send a message at <contact@dalibo.com>.

[open an issue]: https://gitlab.com/dalibo/postgresql_anonymizer/issues



