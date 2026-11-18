Selective Masking (BETA)
===============================================================================

Principle
--------------------------------------------------------------------------------

In some context, it is relevant to mask only certain row of a table.

The selective masking syntax allows to filter based on a condition.

This feature is currently under heavy development. This implementation of
Selective Masking is provided for testing purpose only. Major breaking changes
may be introduced at any time and we may even remove this feature entirely
if we feel it does not reach our standard of quality and stability

Example
--------------------------------------------------------------------------------

Imagine a `users` table containing all the users of an application.

``` sql

SELECT * FROM users;
 id | login |            password             | admin
----+-------+---------------------------------+-------
  1 | alice | adfsqfcksqhdqijsdizjdfiqqlq<iqq | f
  2 | bob   | a_very_bad_password             | t
  3 | carol | 1234                            | f

```

We want to anonymize the login and password columns with the 2 masking rules
below:

``` sql
SECURITY LABEL FOR anon ON COLUMN users.login    IS 'MASKED WITH VALUE NULL';
SECURITY LABEL FOR anon ON COLUMN users.password IS 'MASKED WITH VALUE NULL';
```

We may want to anonymize most of the users in the table while keeping the real
data of the administrators so that they can still use the application in
anonymized environments.

We can add a rule on table to filter out all the admin users:

``` sql
SECURITY LABEL FOR anon ON TABLE users IS 'MASKED WHEN admin IS FALSE';
```

Now let's anonymize the table:

``` sql
SELECT anon.anonymize_table('users');

SELECT * FROM users;
 id | login |            password             | admin
----+-------+---------------------------------+-------
  1 |       |                                 | f
  2 | bob   | a_very_bad_password             | t
  3 |       |                                 | f
```


**NOTE:** The Selective Masking is incompatible with [Sampling].

[Sampling]: sampling.md
