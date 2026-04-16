Generalization
===============================================================================


Reducing the accuracy of sensitive data
--------------------------------------------------------------------------------

The idea of generalization is to replace data with a broader, less accurate
value. For instance, instead of saying "Bob is 28 years old", you can say
"Bob is between 20 and 30 years old". This is interesting for analytics because
the data remains true while avoiding the risk of re-identification.

Generalization is a way to achieve [k-anonymity].

PostgreSQL can handle generalization very easily with the [RANGE] data types,
a very powerful way to store and manipulate a set of values contained between
a lower and an upper bound.

[k-anonymity]: #k-anonymity
[RANGE]: https://www.postgresql.org/docs/current/rangetypes.html


Example
--------------------------------------------------------------------------------

Here's a basic table containing medical data:

```sql
# SELECT * FROM patient;
     ssn     | firstname | zipcode |   birth    |    disease
-------------+-----------+---------+------------+---------------
 253-51-6170 | Alice     |   47012 | 1989-12-29 | Heart Disease
 091-20-0543 | Bob       |   42678 | 1979-03-22 | Allergy
 565-94-1926 | Caroline  |   42678 | 1971-07-22 | Heart Disease
 510-56-7882 | Eleanor   |   47909 | 1989-12-15 | Acne
 098-24-5548 | David     |   47905 | 1997-03-04 | Flu
 118-49-5228 | Jean      |   47511 | 1993-09-14 | Flu
 263-50-7396 | Tim       |   47900 | 1981-02-25 | Heart Disease
 109-99-6362 | Bernard   |   47168 | 1992-01-03 | Asthma
 287-17-2794 | Sophie    |   42020 | 1972-07-14 | Asthma
 409-28-2014 | Arnold    |   47000 | 1999-11-20 | Diabetes
(10 rows)
```

We want the anonymized data to remain **true** because it will be
used for statistics. We can build a view upon this table to remove
useless columns and generalize the indirect identifiers :

```sql
CREATE MATERIALIZED VIEW generalized_patient AS
SELECT
  'REDACTED'::TEXT AS firstname,
  anon.generalize_int4range(zipcode,1000) AS zipcode,
  anon.generalize_daterange(birth,'decade') AS birth,
  disease
FROM patient;
```

This will give us a less accurate view of the data:

```sql
# SELECT * FROM generalized_patient;
 firstname |    zipcode    |          birth          |    disease
-----------+---------------+-------------------------+---------------
 REDACTED  | [47000,48000) | [1980-01-01,1990-01-01) | Heart Disease
 REDACTED  | [42000,43000) | [1970-01-01,1980-01-01) | Allergy
 REDACTED  | [42000,43000) | [1970-01-01,1980-01-01) | Heart Disease
 REDACTED  | [47000,48000) | [1980-01-01,1990-01-01) | Acne
 REDACTED  | [47000,48000) | [1990-01-01,2000-01-01) | Flu
 REDACTED  | [47000,48000) | [1990-01-01,2000-01-01) | Flu
 REDACTED  | [47000,48000) | [1980-01-01,1990-01-01) | Heart Disease
 REDACTED  | [47000,48000) | [1990-01-01,2000-01-01) | Asthma
 REDACTED  | [42000,43000) | [1970-01-01,1980-01-01) | Asthma
 REDACTED  | [47000,48000) | [1990-01-01,2000-01-01) | Diabetes
(10 rows)
```

Generalization Functions
--------------------------------------------------------------------------------

PostgreSQL Anonymizer provides 6 generalization functions. One for each [RANGE]
type. Generally these functions take the original value as the first parameter,
and a second parameter for the length of each step.

For numeric values :

* `anon.generalize_int4range(42,5)` returns the range `[40,45)`
* `anon.generalize_int8range(12345,1000)` returns the range `[12000,13000)`
* `anon.generalize_numrange(42.32378,10)` returns the range `[40,50)`

For time values :

* `anon.generalize_tsrange('1904-11-07','year')` returns `['1904-01-01','1905-01-01')`
* `anon.generalize_tstzrange('1904-11-07','week')` returns `['1904-11-07','1904-11-14')`
* `anon.generalize_daterange('1904-11-07','decade')` returns `[1900-01-01,1910-01-01)`

The possible steps are : microseconds, milliseconds, second, minute, hour, day, week,
month, year, decade, century and millennium.



Limitations
--------------------------------------------------------------------------------

### Singling out and extreme values

"Singling Out" is the possibility to isolate an individual in a dataset by using
extreme value or exceptional values.

For example:

```sql
# SELECT * FROM employees;

  id  |  name          | job  | salary
------+----------------+------+--------
 1578 | xkjefus3sfzd   | NULL |    1498
 2552 | cksnd2se5dfa   | NULL |    2257
 5301 | fnefckndc2xn   | NULL |   45489
 7114 | npodn5ltyp3d   | NULL |    1821
```

In this table, we can see that a particular employee has a very high salary,
very far from the average salary. Therefore this person is probably the CEO
of the company.

With generalization, this is important because the size of the range (the "step")
must be wide enough to prevent the identification of one single individual.

[k-anonymity] is a way to assess this risk.


### Generalization is not compatible with dynamic masking

By definition, with generalization the data remains true, but the column type
is changed.

This means that the transformation is not transparent, and therefore it cannot
be used with [dynamic masking].

[dynamic masking]: dynamic_masking.md

k-anonymity
--------------------------------------------------------------------------------

k-anonymity is an industry-standard term used to describe a property of an
anonymized dataset. The k-anonymity principle states that within a
given dataset, any anonymized individual cannot be distinguished from at
least `k-1` other individuals. In other words, k-anonymity might be described
as a "hiding in the crowd" guarantee. A low value of `k` indicates there's a risk
of re-identification using linkage with other data sources.

You can evaluate the k-anonymity factor of a table in 2 steps :

Step 1: First define the columns that are indirect identifiers (also known
as [quasi identifiers]) like this:

```sql
SECURITY LABEL FOR k_anonymity ON COLUMN patient.firstname
IS 'INDIRECT IDENTIFIER';

SECURITY LABEL FOR k_anonymity ON COLUMN patient.zipcode
IS 'INDIRECT IDENTIFIER';

SECURITY LABEL FOR k_anonymity ON COLUMN patient.birth
IS 'INDIRECT IDENTIFIER';
```

Step 2: Once the indirect identifiers are declared :

```sql
SELECT anon.k_anonymity('generalized_patient')
```

The higher the value, the better...

[quasi identifiers]: https://en.wikipedia.org/wiki/Quasi-identifier

References
--------------------------------------------------------------------------------

* [How Google Anonymizes Data](https://policies.google.com/technologies/anonymization)
