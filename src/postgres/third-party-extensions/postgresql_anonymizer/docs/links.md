Ideas and Resources
================================================================================

Videos / Presentations
--------------------------------------------------------------------------------

* French: https://www.youtube.com/watch?v=KGSlp4UygdU
* English: https://www.youtube.com/watch?v=niIIFL4s-L8
* Chinese: https://www.youtube.com/watch?v=n9atI31FcSM

Similar Tools
--------------------------------------------------------------------------------

Here's a list of **open-source** projects with similar goals. AS the PostgreSQL
Anonymizer extension is often compared to some of them, we try to maintain the
feature matrix below:

<!-- rumdl-disable MD033 -->

|  Name                   | rules<br>syntax | static<br>masking | dynamic<br>masking | backup<br>masking | replica<br>masking | FDW<br>masking |
| ----------------------- | --------------- | ----------------- | ------------------ | ----------------- | ------------------ | -------------- |
| [PostgreSQL Anonymizer] | SQL             |       ✅          |        ✅          |    ✅             |   ✅               |   ✅           |
| [database anonymizer]   | YAML            |       -           |        -           |    ✅             |   -                |   -            |
| [greenmask]             | YAML            |       -           |        -           |    ✅             |   -                |   -            |
| [pg_anon]               | JSON            |       -           |        -           |    ✅             |   -                |   -            |
| [pg_anonymize]          | SQL             |       -           |        ✅          |    ✅             |   -                |   -            |
| [pg_diffix]             | SQL             |       -           |        ✅          |    -              |   -                |   -            |
| [pg-anonymizer]         | JS              |       -           |        -           |    ✅             |   -                |   -            |
| [pg-mask]               | SQL             |       -           |        -           |    ✅             |   -                |   -            |
| [pganonymize]           | YAML            |       -           |        -           |    ✅             |   -                |   -            |
| [pgantomizer]           | YAML            |       -           |        -           |    ✅             |   -                |   -            |
| [pgEdge Anonymizer]     | YAML            |       ✅          |        -           |    -              |   -                |   -            |
| [pgstream]              | YAML            |       -           |        -           |    -              |   ✅               |   -            |


[PostgreSQL Anonymizer]: https://labs.dalibo.com/postgresql_anonymizer
[database anonymizer]: https://gitnet.fr/deblan/database-anonymizer
[greenmask]: https://github.com/GreenmaskIO/greenmask
[pganonymize]: https://github.com/rheinwerk-verlag/pganonymize
[pgantomizer]: https://github.com/asgeirrr/pgantomizer
[pg_diffix]: https://github.com/diffix/pg_diffix
[pg_anonymize]: https://github.com/rjuju/pg_anonymize
[pg-anonymizer]: https://github.com/rap2hpoutre/pg-anonymizer
[pg-mask]: https://github.com/rpobulic/pg-mask
[pgEdge Anonymizer]: https://github.com/pgEdge/pgedge-anonymizer
[pgstream]: https://github.com/xataio/pgstream
[pg_anon]: https://github.com/TantorLabs/pg_anon


Similar Implementations
--------------------------------------------------------------------------------

* [Dynamic Data Masking With MS SQL Server](https://docs.microsoft.com/en-us/sql/relational-databases/security/dynamic-data-masking)

* [Citus : Using search_path and views to hide columns for reporting with Postgres](https://www.citusdata.com/blog/2018/07/03/masking-columns-in-postgresql/)

* [MariaDB : Masking with maxscale](https://mariadb.com/kb/en/mariadb-enterprise/mariadb-maxscale-21-masking/)


GDPR
--------------------------------------------------------------------------------

* [Ultimate Guide to Data Anonymization](https://piwik.pro/blog/the-ultimate-guide-to-data-anonymization-in-analytics/)

* [UK ICO Anonymisation Code of Practice](https://ico.org.uk/media/1061/anonymisation-code.pdf)

* [L. Sweeney, Simple Demographics Often Identify People Uniquely, 2000](https://dataprivacylab.org/projects/identifiability/paper1.pdf)

* [How Google anonymizes data](https://policies.google.com/technologies/anonymization?hl=en)

* [IAPP's Guide To Anonymisation](https://iapp.org/media/pdf/resource_center/Guide_to_Anonymisation.pdf)


Concepts
--------------------------------------------------------------------------------

* [Differential_Privacy](https://en.wikipedia.org/wiki/Differential_Privacy)

* [K-Anonymity](https://en.wikipedia.org/wiki/K-anonymity)


Academic Research
--------------------------------------------------------------------------------

* L. Sweeney. k-anonymity: a model for protecting privacy. International Journal
  on Uncertainty, Fuzziness and Knowledge-based Systems, 10 (5), 2002,
  pp. 557-570.
  <https://epic.org/wp-content/uploads/privacy/reidentification/Sweeney_Article.pdf>

* A. Narayanan and V. Shmatikov, “Robust de-anonymization of large sparse
  datasets,” in 29th IEEE Symposium on Security and Privacy, 2008, pp. 111–125.
  <https://www.cs.cornell.edu/~shmat/shmat_oak08netflix.pdf>
