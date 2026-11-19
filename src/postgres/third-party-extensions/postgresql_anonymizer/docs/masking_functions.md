Various Masking Strategies
==============================================================================

The extension provides functions to implement 8 main anonymization strategies:

* [Destruction]
* [Adding Noise]
* [Local Differential Privacy (LDP)]
* [Randomization]
* [Faking]
* [Advanced Faking]
* [Pseudonymization]
* [Generic Hashing]
* [Partial scrambling]
* [Conditional masking]
* [Generalization]
* [Using pg_catalog functions]
* [Image bluring]
* [Write your own Masks !]

[Destruction]: #destruction
[Adding Noise]: #adding-noise
[Local Differential Privacy (LDP)]: #local-differential-privacy-ldp
[Randomization]: #randomization
[Faking]: #faking
[Advanced Faking]: #advanced-faking
[Pseudonymization]: #pseudonymization
[Generic Hashing]: #generic-hashing
[Partial scrambling]: #partial-scrambling
[Conditional masking]: #conditional-masking
[Generalization]: #generalization
[Shuffling]: static_masking.md#shuffling
[Using pg_catalog functions]: #using-pg_catalog-functions
[Image bluring]: #image-bluring
[Write your own Masks !]: #write-your-own-masks

Depending on your data, you may need to use different strategies on different
columns :

* For names and other 'direct identifiers' , [Faking] is often useful
* [Shuffling] is convenient for foreign keys
* [Adding Noise] is interesting for numeric values and dates
* [Partial Scrambling] is perfect for email address and phone numbers
* etc.

Destruction
------------------------------------------------------------------------------

First of all, the fastest and safest way to anonymize a data is to destroy it
:-)

In many cases, the best approach to hide the content of a column is to replace
all the values with a single static value.

For instance, you can replace a entire column by the word 'CONFIDENTIAL' like
this:

```sql
SECURITY LABEL FOR anon
  ON COLUMN users.address
  IS 'MASKED WITH VALUE ''CONFIDENTIAL'' ';
```


Adding Noise
------------------------------------------------------------------------------

This is also called **Variance**. The idea is to "shift" dates and numeric
values. For example, by applying a +/- 10% variance to a salary column, the
dataset will remain meaningful.

* `anon.noise(original_value,ratio)` where original_value can be an `integer`,
  a `bigint` or a `double precision`. If the ratio is 0.33, the return value
  will be the original value randomly shifted with a ratio of +/- 33%

* `anon.dnoise(original_value, interval)` where original_value can be a date, a
  timestamp, or a time. If interval = '2 days', the return value will be the
  original value randomly shifted by +/- 2 days

* `anon.noisy_gps_coordinates(origin, radius, is_metric)` returns GPS
  coordinates located in a radius (default `10.0`) around a point. The radius can
  be expressed in kimometers (default) or miles.

    ```sql
    CREATE TABLE customer( .... , gps_location geometry(Point, 4326));

    SECURITY LABEL FOR anon ON COLUMN customer.gps_location
    IS 'MASKED WITH FUNCTION ST_SetSRID(
        noisy_gps_coordinates(gps_location::point, 10.0, true)::geometry,
        4326
    )';
    ```

**WARNING** : The `noise()` masking functions are vulnerable to a form of
repeat attack, especially with [Dynamic Masking]. A masked user can guess
an original value by requesting its masked value multiple times and then simply
use the `AVG()` function to get a close approximation. (See
`demo/noise_reduction_attack.sql` for more details). In a nutshell, these
functions are best fitted for [Anonymous Dumps] and [Static Masking].
They should be avoided when using [Dynamic Masking].

[Anonymous Dumps]: anonymous_dumps.md
[Static Masking]: static_masking.md
[Dynamic Masking]: dynamic_masking.md



Local Differential Privacy (LDP)
------------------------------------------------------------------------------

**Local Differential Privacy** is a stronger approach to adding noise. Unlike
the `noise()` functions above, LDP provides a formal mathematical guarantee:
given the output, an observer cannot determine the original value with high
confidence, no matter what auxiliary information they have. The strength of
this guarantee is controlled by a parameter called **epsilon** -- a smaller
epsilon means stronger privacy but less accuracy.

This is particularly useful for **survey data** and **categorical values**
(e.g. ratings, age brackets, answer choices) where you want to collect
aggregate statistics while protecting individual responses.

### GRRM (Generalized Randomized Response Mechanism)

The GRRM method works on categorical values represented as integers from 1 to
`max_v`. Each value is either kept truthfully or replaced with a uniformly
random alternative:

```sql
-- Perturb a rating (1-5) with epsilon = 1.0
SELECT anon.ldp_grrm(3, 1.0, 5);
```

You can also specify the privacy level using a "kept percentage" (PTTT) instead
of epsilon. A kept percentage of 0.65 means roughly 65% of the differential
probability is allocated to the true value:

```sql
SELECT anon.ldp_grrm_pttt(3, 0.65, 5);
```

To use this as a masking rule:

```sql
SECURITY LABEL FOR anon
  ON COLUMN survey.satisfaction_rating
  IS 'MASKED WITH FUNCTION anon.ldp_grrm(satisfaction_rating, 1.0, 5)';
```

### Inspecting the privacy parameters

You can check the probabilities implied by your choice of epsilon:

```sql
-- Probability of keeping the true value
SELECT anon.ldp_truth_probability(1.0, 5);

-- Probability of each specific lie value
SELECT anon.ldp_lie_probability(1.0, 5);
```


Randomization
------------------------------------------------------------------------------

The extension provides a large choice of functions to generate purely random
data :

### Basic Random values

* `anon.random_date()` returns a date
* `anon.random_string(n)` returns a TEXT value containing `n` letters
* `anon.random_zip()` returns a 5-digit code
* `anon.random_phone(p)` returns a 8-digit phone with `p` as a prefix
* `anon.random_hash(seed)` returns a hash of a random string for a given seed
* `anon.random_gps_coordinates()` returns random GPS coordinates
* `anon.random_point_in_box(Box)` returns a random point in Box

### Random between

To pick any value inside between two bounds:

* `anon.random_date_between(d1,d2)` returns a date between `d1` and `d2`
* `anon.random_int_between(i1,i2)` returns an integer between `i1` and `i2`
* `anon.random_bigint_between(b1,b2)` returns a bigint between `b1` and `b2`

**NOTE**: With these functions, the lower and upper bounds are included.
For instance `anon.random_int_between(1,3)` returns either 1, 2 or 3.

For more advanced interval descriptions, check out the [Random in Range]
section.

### Random in Array

The `random_in` function returns an element a given array

For example:

* `anon.random_in(ARRAY[1,2,3])` returns an int between 1 and 3
* `anon.random_in(ARRAY['red','green','blue'])` returns a text


### Random in Enum

This is one especially useful when working with `ENUM` types!

* `anon.random_in_enum(variable_of_an_enum_type)` returns any val



```sql
CREATE TYPE card AS ENUM ('visa', 'mastercard', ‘amex’);

SELECT anon.random_in_enum(NULL::CARD);
 random_in_enum
----------------
 mastercard

CREATE TABLE customer (
  id INT,
  ...
  credit_card CARD
);

SECURITY LABEL FOR anon ON COLUMN customer.creditcard
IS 'MASKED WITH FUNCTION anon.random_in_enum(creditcard)'
```


### Random in Range

[RANGE types] are a powerful way to describe an interval of values, where can
define inclusive or excluvive bounds:

<https://www.postgresql.org/docs/current/rangetypes.html#RANGETYPES-EXAMPLES>

There a function for each subtype of range:

* `anon.random_in_int4range('[5,6)')` returns an INT of value 5
* `anon.random_in_int8range('(6,7]')` returns a BIGINT of value 7
* `anon.random_in_numrange('[0.1,0.9]')` returns a NUMERIC between 0.1 and 0.9
* `anon.random_in_daterange('[2001-01-01, 2001-12-31)')` returns a date in 2001
* `anon.random_in_tsrange('[2022-10-01,2022-10-31]')` returns a
  TIMESTAMP in october 2022
* `anon.random_in_tstzrange('[2022-10-01,2022-10-31]')` returns a
  TIMESTAMP WITH TIMEZONE in october 2022

**NOTE:** It is not possible to get a random value from a RANGE with an
infinite bound. For example `anon.random_in_int4range('[2022,)')` returns NULL.


[RANGE types]: https://www.postgresql.org/docs/current/rangetypes.html

### Random Sequence ID

When masking a SERIAL columns it can be useful to general a `UNIQUE` value
based on a sequence.

* `anon.random_id()` returns a BIGINT
* `anon.random_id_int()` returns a INT
* `anon.random_id_small_int()` returns a SMALLINT

Each call to these functions will return a incremented value much like the
[nextval()] function.

[nextval()]: https://www.postgresql.org/docs/current/functions-sequence.html

At any time, you can reset the current sequence value with a new value.
For instance:

``` sql
SELECT pg_catalog.setval('anon.random_id_seq', 42);
```


Faking
------------------------------------------------------------------------------

The idea of **Faking** is to replace sensitive data with **random-but-plausible**
values. The goal is to avoid any identification from the data record while
remaining suitable for testing, data analysis and data processing.

In order to use the faking functions, you have to `init()` the extension
in your database first:

```sql
SELECT anon.init();
```

The `init()` function will import a default dataset of random data (iban,
names, cities, etc.).

> This dataset is in English and very small ( 1000 values for each
> category ). If you want to use localized data or load a
> specific dataset, please read the [Custom Fake Data] section.

[Custom Fake Data]: custom_fake_data.md

Once the fake data is loaded, you have access to these faking functions:

* `anon.fake_address()` returns a complete post address
* `anon.fake_city()` returns an existing city
* `anon.fake_country()` returns a country
* `anon.fake_company()` returns a generic company name
* `anon.fake_email()` returns a valid email address
* `anon.fake_first_name()` returns a generic first name
* `anon.fake_iban()` returns a valid IBAN
* `anon.fake_last_name()` returns a generic last name
* `anon.fake_postcode()` returns a valid zipcode
* `anon.fake_siret()` returns a valid SIRET

For TEXT and VARCHAR columns, you can use the classic [Lorem Ipsum] generator:

* `anon.lorem_ipsum()` returns 5 paragraphs
* `anon.lorem_ipsum(2)` returns 2 paragraphs
* `anon.lorem_ipsum( paragraphs := 4 )` returns 4 paragraphs
* `anon.lorem_ipsum( words := 20 )` returns 20 words
* `anon.lorem_ipsum( characters := 7 )` returns 7 characters
* `anon.lorem_ipsum( characters := anon.length(table.column) )` returns the same
   amount of characters as the original string

[Lorem Ipsum]: https://lipsum.com

Advanced Faking
------------------------------------------------------------------------------

Generating fake data is a complex topic. The `fake_` functions provided above
are limited to basic use case. For more advanced faking methods, in particular
if you are looking for **localized fake data**, PostgreSQL Anonymizer
provides an advanced faking engine with localisation support.

This engine ([fake-rs]) is available via more than 70 functions with the
`dummy_` prefix:

*tips:*

*The fake_\* and dummy_\* functions achieve the same goal.*

*The fake_\* functions are the first implementation in pl/pgsql. They were
introduced in Version 1. It's a rather naïve and limited approach.*

*The dummy_\* functions are a new implementation based on a Rust library.
It provides a more advanced fake generator and adds localization.
It was introduced in Version 2.*

*New users should always prefer the dummy_\* functions. The fake_\* functions
are kept for backward compatibility.*

<!--
SELECT format(
  '* %I.%I(%s)', ns.nspname, p.proname, oidvectortypes(p.proargtypes)
)
FROM pg_proc p
INNER JOIN pg_namespace ns ON (p.pronamespace = ns.oid)
WHERE ns.nspname = 'anon'
  AND p.proname LIKE 'dummy_%'
  AND p.proname NOT LIKE '%_locale'
ORDER BY 1;
-->

* anon.dummy_bic()
* anon.dummy_bs()
* anon.dummy_bs_adj()
* anon.dummy_bs_noun()
* anon.dummy_bs_verb()
* anon.dummy_building_number()
* anon.dummy_buzzword()
* anon.dummy_buzzword_middle()
* anon.dummy_buzzword_tail()
* anon.dummy_catchphrase()
* anon.dummy_cell_number()
* anon.dummy_city_name()
* anon.dummy_city_prefix()
* anon.dummy_city_suffix()
* anon.dummy_color()
* anon.dummy_company_name()
* anon.dummy_company_suffix()
* anon.dummy_country_code()
* anon.dummy_country_name()
* anon.dummy_credit_card_number()
* anon.dummy_currency_code()
* anon.dummy_currency_name()
* anon.dummy_currency_symbol()
* anon.dummy_dir_path()
* anon.dummy_domain_suffix()
* anon.dummy_file_extension()
* anon.dummy_file_name()
* anon.dummy_file_path()
* anon.dummy_first_name()
* anon.dummy_free_email()
* anon.dummy_free_email_provider()
* anon.dummy_health_insurance_code()
* anon.dummy_hex_color()
* anon.dummy_hsl_color()
* anon.dummy_hsla_color()
* anon.dummy_industry()
* anon.dummy_ip()
* anon.dummy_ipv4()
* anon.dummy_ipv6()
* anon.dummy_isbn()
* anon.dummy_isbn13()
* anon.dummy_isin()
* anon.dummy_last_name()
* anon.dummy_latitude()
* anon.dummy_licence_plate()
* anon.dummy_longitude()
* anon.dummy_mac_address()
* anon.dummy_name()
* anon.dummy_name_with_title()
* anon.dummy_phone_number()
* anon.dummy_post_code()
* anon.dummy_profession()
* anon.dummy_rfc_status_code()
* anon.dummy_rgb_color()
* anon.dummy_rgba_color()
* anon.dummy_safe_email()
* anon.dummy_secondary_address()
* anon.dummy_secondary_address_type()
* anon.dummy_state_abbr()
* anon.dummy_state_name()
* anon.dummy_street_name()
* anon.dummy_street_suffix()
* anon.dummy_suffix()
* anon.dummy_timezone()
* anon.dummy_title()
* anon.dummy_user_agent()
* anon.dummy_username()
* anon.dummy_uuidv1()
* anon.dummy_uuidv3()
* anon.dummy_uuidv4()
* anon.dummy_uuidv5()
* anon.dummy_valid_statux_code()
* anon.dummy_word()
* anon.dummy_words(int4range)
* anon.dummy_zip_code()

For each of this function, you can add the `_locale(...)` suffix and specify
in which local context you want.

For example:

```sql

SELECT anon.dummy_last_name();
 dummy_last_name
------------------------
 Tillman

SELECT anon.dummy_last_name_locale('fr_FR');
 dummy_last_name_locale
------------------------
 Granier

SELECT anon.dummy_last_name_locale('pt_BR');
 dummy_last_name_locale
------------------------
 Barreto
```

Currently 7 locales are available: ar_SA, en_US(default), fr_FR, ja_JP,
pt_BR, zh_CN, zh_TW.

Not that some `dummy_` functions are not implemented for certain locales.
If you wish to contribute or ask for missing fake data, please contact directly
the [fake-rs] project, which is the library that this extension is using under the
hood !

[fake-rs]: https://github.com/cksac/fake-rs


Pseudonymization
------------------------------------------------------------------------------

Pseudonymization is similar to [Faking] in the sense that it generates
realistic values. The main difference is that the pseudonymization is
deterministic : the functions always will return the same fake value based
on a seed and an optional salt.

In order to use the faking functions, you have to `init()` the extension
in your database first:

```sql
SELECT anon.init();
```

Once the fake data is loaded you have access to 10 pseudo functions:

* `anon.pseudo_first_name(seed,salt)` returns a generic first name
* `anon.pseudo_last_name(seed,salt)` returns a generic last name
* `anon.pseudo_email(seed,salt)` returns a valid email address
* `anon.pseudo_city(seed,salt)` returns an existing city
* `anon.pseudo_country(seed,salt)` returns a country
* `anon.pseudo_company(seed,salt)` returns a generic company name
* `anon.pseudo_iban(seed,salt)` returns a valid IBAN
* `anon.pseudo_siret(seed,salt)` returns a valid SIRET


The second argument (`salt`) is optional. You can call each function with
only the seed like this `anon.pseudo_city('bob')`. The salt is here to increase
complexity and avoid dictionary and brute force attacks (see warning below).
If a specific salt is not given, the value of the `anon.salt` GUC parameter is
used instead (see the [Generic Hashing] section for more details).

The seed can be any information related to the subject. For instance, we can
consistently generate the same fake email address for a given person by using
her login as the seed :

```sql
SECURITY LABEL FOR anon
  ON COLUMN users.emailaddress
  IS 'MASKED WITH FUNCTION anon.pseudo_email(users.login) ';
```

**NOTE**: You may want to produce unique values using a pseudonymization
function. For instance, if you want to mask an `email` column that is declared
as `UNIQUE`. In this case, you will need to initialize the extension with a fake
dataset that is **way bigger** than the numbers of rows of the table. Otherwise you
may see some "collisions" happening, i.e. two different original values producing
the same pseudo value.


It is also possible to pseudonymize a primary key using:

* `anon.pseudo_shift(id)` returns a shifted version of the id
* `anon.pseudo_xor(id)` returns an exclusive OR value of the id

Both `anon.pseudo_shift(BIGINT)` and `anon.pseudo_xor(BIGINT)` functions use
a secret value (`anon.shift`) to pseudonymize the primary key. That secret value
can be initialized randomly with `anon.set_shift()` or defined with
`anon.set_shift(INT)`.

This is very useful to replace `anon.random_id()` when using [Backup Masking].

[Backup Masking]: https://postgresql-anonymizer.readthedocs.io/en/stable/anonymous_dumps/

**⚠️ WARNING**: Pseudonymization is often confused with anonymization but in fact
they serve 2 different purposes : `pseudonymization` is a way to **protect** the
personal information but the pseudonymized data is still "linked" to the real data.
The GDPR makes it very clear that personal data which has undergone
pseudonymization is still related to a person. (see [GDPR Recital 26])

[GDPR Recital 26]: https://www.privacy-regulation.eu/en/recital-26-GDPR.htm


Generic hashing
-------------------------------------------------------------------------------

Hashing is another pseudonymization technique (see **WARNING** above). In
practice it is sometimes useful to generate a determinist hash of the original
data.

For instance, when a pair of primary key / foreign key is a "natural key",
it may contain actual information ( like a customer number containing a birth
date or something similar).

Hashing such columns allows to keep referential integrity intact even for
relatively unusual source data. Therefore, the

* `anon.digest(value,salt,algorithm)` lets you choose a salt, and a hash algorithm
  from a pre-defined list

* `anon.hash(value)` will return a text hash of the value using a secret salt
  (defined by the `anon.salt` parameter) and hash algorithm (defined by the
  `anon.algorithm` parameter). The default value of `anon.algorithm` is
  `sha256` and possible values are: md5, sha224, sha256, sha384 or sha512.
  The default value of `anon.salt` is an empty string. You can modify these
  values with:

    ```sql
    ALTER DATABASE foo SET anon.salt TO 'xsfnjefnjsnfjsnf';
    ALTER DATABASE foo SET anon.algorithm TO 'sha384';
    ```

Keep in mind that hashing is a form a [Pseudonymization]. This means that the
data can be "de-anonymized" using the hashed value and the masking function. If an
attacker gets access to these 2 elements, he or she could re-identify
some persons using `brute force` or `dictionary` attacks. Therefore, **the
salt and the algorithm used to hash the data must be protected with the
same level of security that the original dataset.**

In a nutshell, we recommend that you use the `anon.hash()` function rather than
`anon.digest()` because the salt will not appear clearly in the masking rule.

Furthermore: in practice the hash function will return a long string of character
like this:

```sql
SELECT anon.hash('bob');
                                  hash
----------------------------------------------------------------------------------------------------------------------------------
95b6accef02c5a725a8c9abf19ab5575f99ca3d9997984181e4b3f81d96cbca4d0977d694ac490350e01d0d213639909987ef52de8e44d6258d536c55e427397
```

For some columns, this may be too long and you may have to cut some parts the
hash in order to fit into the column. For instance, if you have a foreign key
based on a phone number and the column is a VARCHAR(12) you can transform the
data like this:

```sql
SECURITY LABEL FOR anon ON COLUMN people.phone_number
IS 'MASKED WITH FUNCTION anon.left(anon.hash(phone_number),12)';

SECURITY LABEL FOR anon ON COLUMN call_history.fk_phone_number
IS 'MASKED WITH FUNCTION anon.left(anon.hash(fk_phone_number),12)';
```

Of course, cutting the hash value to 12 characters will increase the risk
of "collision" (2 different values having the same fake hash). In such
case, it's up to you to evaluate this risk.

**⚠️ WARNING**: The hashing functions will fail when the input contains
an unescaped character (especially a single backslash). In most situation,
this is the sign of a bug in the application, generally when data input is not
sanitized properly. Users who really want to mask unescaped characters with
this function should disable the `standard_conforming_strings` parameter.
See [Issue 539] for more details.

[Issue 539]: https://gitlab.com/dalibo/postgresql_anonymizer/-/issues/539


Partial Scrambling
-------------------------------------------------------------------------------

**Partial scrambling** leaves out some part of the data.
For instance : a credit card number can be replaced by '40XX XXXX XXXX XX96'.

2 functions are available:

* `anon.partial('abcdefgh',1,'xxxx',3)` will return 'axxxxfgh';
* `anon.partial_email('daamien@gmail.com')` will become 'da******@gm******.com'

Conditional Masking
-------------------------------------------------------------------------------

In some situations, you may want to apply a masking filter only for some value
or for a limited number of lines in the table.

For instance, if you want to "preserve NULL values", i.e. masking only the lines
that contains a value, you can use the `anon.ternary` function, which works
like a `CASE WHEN x THEN y ELSE z` statement:

```sql
SECURITY LABEL FOR anon ON COLUMN player.score
  IS 'MASKED WITH FUNCTION anon.ternary(score IS NULL,
                                        NULL,
                                        anon.random_int_between(0,100));
```

You can also use the `anon.ternary` function to keep a ratio of NULL values in
the otherwise anonymized data like in the following example where each line as
as 10% chance to be a NULL value:

```sql
SECURITY LABEL FOR anon ON COLUMN player.score
  IS 'MASKED WITH FUNCTION anon.ternary(pg_catalog.random() <= .1,
                                        NULL,
                                        anon.random_int_between(0,100));
```

You may also want to exclude some lines within the table. Like keeping the
password of some users so that they still may be able to connect to a testing
deployment of your application:

```sql
SECURITY LABEL FOR anon ON COLUMN account.password
  IS 'MASKED WITH FUNCTION anon.ternary( id > 1000, NULL::TEXT, password)';
```

**WARNING** : Conditional masking may create a partially deterministic
"connection" between the original data and the masked data. And that connection
can be used to retrieve personal information from the masked data. For instance,
if NULL values are preserved for a "deceased_date" column, it will reveal which
persons are still actually alive... In a nutshell: conditional masking may often
produce a dataset that is not fully anonymized and therefore would still
technically contain personal information.


Generalization
-------------------------------------------------------------------------------

Generalization is the principle of replacing the original value by a range
containing this value. For instance, instead of saying 'Paul is 42 years old',
you would say 'Paul is between 40 and 50 years old'.

> The generalization functions are a data type transformation. Therefore it is
> not possible to use them with the dynamic masking engine. However they are
> useful to create anonymized views. See example below.

Let's imagine a table containing health information:

```sql
SELECT * FROM patient;
 id |   name   |  zipcode |   birth    |    disease
----+----------+----------+------------+---------------
  1 | Alice    |    47678 | 1979-12-29 | Heart Disease
  2 | Bob      |    47678 | 1959-03-22 | Heart Disease
  3 | Caroline |    47678 | 1988-07-22 | Heart Disease
  4 | David    |    47905 | 1997-03-04 | Flu
  5 | Eleanor  |    47909 | 1999-12-15 | Heart Disease
  6 | Frank    |    47906 | 1968-07-04 | Cancer
  7 | Geri     |    47605 | 1977-10-30 | Heart Disease
  8 | Harry    |    47673 | 1978-06-13 | Cancer
  9 | Ingrid   |    47607 | 1991-12-12 | Cancer
```

We can build a view upon this table to suppress some columns ( `SSN`
and `name` ) and generalize the zipcode and the birth date like
this:

```sql
CREATE VIEW anonymized_patient AS
SELECT
    'REDACTED' AS lastname,
    anon.generalize_int4range(zipcode,100) AS zipcode,
    anon.generalize_tsrange(birth,'decade') AS birth
    disease
FROM patients;
```

The anonymized table now looks like that:

```sql
SELECT * FROM anonymized_patient;
 lastname |   zipcode     |           birth             |    disease
----------+---------------+-----------------------------+---------------
 REDACTED | [47600,47700) | ["1970-01-01","1980-01-01") | Heart Disease
 REDACTED | [47600,47700) | ["1950-01-01","1960-01-01") | Heart Disease
 REDACTED | [47600,47700) | ["1980-01-01","1990-01-01") | Heart Disease
 REDACTED | [47900,48000) | ["1990-01-01","2000-01-01") | Flu
 REDACTED | [47900,48000) | ["1990-01-01","2000-01-01") | Heart Disease
 REDACTED | [47900,48000) | ["1960-01-01","1970-01-01") | Cancer
 REDACTED | [47600,47700) | ["1970-01-01","1980-01-01") | Heart Disease
 REDACTED | [47600,47700) | ["1970-01-01","1980-01-01") | Cancer
 REDACTED | [47600,47700) | ["1990-01-01","2000-01-01") | Cancer
```


The generalized values are still useful for statistics because they remain
true, but they are less accurate, and therefore reduce the risk of
re-identification.

PostgreSQL offers several [RANGE] data types which are perfect for dates and
numeric values.

For numeric values, 3 functions are available:

* `generalize_int4range(value, step)`
* `generalize_int8range(value, step)`
* `generalize_numrange(value, step)`

...where `value` is the data that will be generalized, and `step` is the size of
each range.


[RANGE]: https://www.postgresql.org/docs/current/rangetypes.html


Using `pg_catalog` functions
------------------------------------------------------------------------------

Since version 1.3, the `pg_catalog` schema is not trusted by default. This is
a security measure designed to prevent users from using sophisticated functions
inside masking rules (such as `pg_catalog.query_to_xml`, `pg_catalog.ts_stat`
or the [system administration functions] ) that should not be used as masking
functions.

[system administration functions]: https://www.postgresql.org/docs/current/functions-admin.html.

However, the extension allows using some useful and safe functions from the
`pg_catalog` schema for your convenience. These are small subset of functions
that are declared as `TRUSTED` for anonymization.

The list of TRUSTED pg_catalog functions is available via the
`anon.pg_trusted_functions` views :


``` sql
SELECT * FROM anon.pg_trusted_functions;
```

If you need to use a pg_catalog function which is not in this list, you can
ask a superuser to trust it with:

``` sql
SECURITY LABEL FOR anon ON FUNCTION pg_catalog.foo IS 'TRUSTED';
```

> Note: Even when [multiple masking policies] are defined, the functions must be
> declared as `TRUSTED` in the "anon" policy and they will be trusted for all
> policies.

[multiple masking policies]: declare_masking_rules.md#multiple_masking_policies

Image bluring
------------------------------------------------------------------------------

Images can show some sensitive data, for example

- A photo concerning personal data.
- A barcode representing personal data.

it is possible to blur this image using

* `anon.image_blur(data,sigma)` returns a bytea
* data type `bytea`: the image data
* sigma type `numeric`: This parameter controls the amount of blurring.
  A higher sigma value results in a more blurred image,
  while a lower sigma value results in a less blurred image.

usage :

```sql
CREATE TABLE images (
    id SERIAL PRIMARY KEY,
    name TEXT NOT NULL,
    image_data BYTEA NOT NULL
);
create extension anon;
SELECT anon.init();

SECURITY LABEL FOR anon ON COLUMN images.image_data
IS 'MASKED WITH FUNCTION anon.image_blur(image_data,1.0)';

SELECT anon.anonymize_database();

```


Write your own Masks !
------------------------------------------------------------------------------

You can also use your own function as a mask. The function must either be
destructive (like [Partial Scrambling]) or insert some randomness in the dataset
(like [Faking]).

<!-- cf. demo/writing_your_own_mask.sql -->

Especially for complex data types, you may have to write your own function.
This will be a common use case if you have to hide certain parts of a JSON field.

For example:

```sql
CREATE TABLE company (
  business_name TEXT,
  info JSONB
)
```

The `info` field contains unstructured data like this:

```sql
SELECT jsonb_pretty(info) FROM company WHERE business_name = 'Soylent Green';
           jsonb_pretty
----------------------------------
 {
     "employees": [
         {
             "lastName": "Doe",
             "firstName": "John"
         },
         {
             "lastName": "Smith",
             "firstName": "Anna"
         },
         {
             "lastName": "Jones",
             "firstName": "Peter"
         }
     ]
 }
(1 row)
```

Using the [PostgreSQL JSON functions and operators], you can walk
through the keys and replace the sensitive values as needed.

[PostgreSQL JSON functions and operators]: https://www.postgresql.org/docs/current/functions-json.html

```sql
CREATE SCHEMA custom_masks;

CREATE FUNCTION custom_masks.remove_last_name(j JSONB)
RETURNS JSONB
VOLATILE
LANGUAGE SQL
AS $func$
SELECT
  json_build_object(
    'employees' ,
    array_agg(
      jsonb_set(e ,'{lastName}', to_jsonb(anon.fake_last_name()))
    )
  )::JSONB
FROM jsonb_array_elements( j->'employees') e
$func$;

-- This step requires superuser privilege
SECURITY LABEL FOR anon ON FUNCTION custom_masks.remove_last_name IS 'TRUSTED';

```

Then check that the function is working correctly:

```sql
SELECT custom_masks.remove_last_name(info) FROM company;
```

When that's ok you can declare this function as the mask of
the `info` field:

```sql
SECURITY LABEL FOR anon ON COLUMN company.info
IS 'MASKED WITH FUNCTION custom_masks.remove_last_name(info)';
```

And try it out !

```sql
# SELECT anonymize_table('company');
# SELECT jsonb_pretty(info) FROM company WHERE business_name = 'Soylent Green';
            jsonb_pretty
-------------------------------------
 {
     "employees": [                 +
         {                          +
             "lastName": "Prawdzik",+
             "firstName": "John"    +
         },                         +
         {                          +
             "lastName": "Baltazor",+
             "firstName": "Anna"    +
         },                         +
         {                          +
             "lastName": "Taylan",  +
             "firstName": "Peter"   +
         }                          +
     ]                              +
 }
(1 row)
```

This is just a quick and dirty example. As you can see, manipulating a
sophisticated JSON structure with SQL is possible, but it can be tricky at
first! There are multiple ways of walking through the keys and updating
values. You will probably have to try different approaches, depending on
your real JSON data and the performance you want to reach.
