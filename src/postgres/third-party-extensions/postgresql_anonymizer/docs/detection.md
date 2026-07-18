Searching for Identifiers
===============================================================================

> WARNING : This feature is at an early stage of development.

As we've seen previously, this extension makes it very easy to
[declare masking rules].

[declare masking rules]: declare_masking_rules.md

However, when you create an anonymization strategy, the hard part is
scanning the database model to find which columns contains direct and indirect
identifiers, and then decide how these identifiers should be masked.

The extension provides a `detect()` function that will search for common
identifier names based on a dictionary. For now, 2 dictionaries are available:
english ('en_US') and french ('fr_FR'). By default, the english dictionary is
used:

```sql
# SELECT anon.detect('en_US');
 table_name |  column_name   | identifiers_category | direct
------------+----------------+----------------------+--------
 customer   | CreditCard     | creditcard           | t
 vendor     | Firstname      | firstname            | t
 customer   | firstname      | firstname            | t
 customer   | id             | account_id           | t
```

The identifier categories are based on the [HIPAA classification].

[HIPAA classification]: https://www.luc.edu/its/aboutits/itspoliciesguidelines/hipaainformation/18hipaaidentifiers/

Limitations
---------------------------------------------------------------------------------

This is an heuristic method in the sense that it may report usefull information,
but it is based on a pragmatic approach that can lead to detection mistakes,
especially:

* `false positive`: a column is reported as an identifier, but it is not.
* `false negative`: a column contains identifiers, but it is not reported

The second one is of course more problematic. In any case, you should only
consider this function as a helping tool, and acknowledge that you still need
to review the entire database model in search of hidden identifiers.

Contribute to the dictionnaries
---------------------------------------------------------------------------------

This detection tool is based on dictionnaries of identifiers. Currently these
dictionnaries contain only a few entries.

For instance, you can see the english identifier dictionary [here].

[here]: https://gitlab.com/dalibo/postgresql_anonymizer/-/blob/master/data/identifier_en_US.csv

You can help us improve this feature by sending us a list of direct and
indirect identifiers you have found in your own data models ! Send us an
email at <contact@dalibo.com> or [open an issue] in the project.

[open an issue]: https://gitlab.com/dalibo/postgresql_anonymizer/-/issues

