Cypher Query Language
=====================

RETURN
------

Defines what to include in the query result set.

Synopsis
~~~~~~~~

::

  RETURN [ DISTINCT ]
         expression [ AS alias ] [, ...]
         [ order-by ]
         [ skip ]
         [ limit ]

Description
~~~~~~~~~~~

*TODO*

WITH
----

Allows query parts to be chained together, piping the results from one to be used as starting points or criteria in the next.

Synopsis
~~~~~~~~

::

  WITH [ DISTINCT ]
       expression [ AS alias ] [, ...]
       [ order-by ]
       [ skip ]
       [ limit ]
       [ where ]

Description
~~~~~~~~~~~

*TODO*

WHERE
-----

Adds constraints to the patterns in a ``MATCH`` or ``OPTIONAL MATCH`` clause or filters the results of a ``WITH`` clause.

Synopsis
~~~~~~~~

::

  WHERE expression

Description
~~~~~~~~~~~

*TODO*

ORDER BY
--------

Is a sub-clause following ``RETURN`` or ``WITH``, and it specifies that the output should be sorted and how.

Synopsis
~~~~~~~~

::

  ORDER BY expression [ ASC | ASCENDING | DESC | DESCENDING ] [, ...]

Description
~~~~~~~~~~~

*TODO*

SKIP
----

Defines from which record to start including the records in the output.

Synopsis
~~~~~~~~

::

  SKIP start

Description
~~~~~~~~~~~

*TODO*

LIMIT
-----

Constrains the number of records in the output.

Synopsis
~~~~~~~~

::

  LIMIT count

Description
~~~~~~~~~~~

*TODO*

SET
---

Sets and updates properties from nodes and expressions given an expression.

Synopsis
~~~~~~~~

::

  SET expression

Description
~~~~~~~~~~~

*TODO*

REMOVE
------

Removes properties from nodes and relationships.

Synopsis
~~~~~~~~

::

  REMOVE property

Description
~~~~~~~~~~~

*TODO*

DELETE
------

Deletes nodes and relationships from the graph. Use ``DETACH`` to delete a node and any relationship it has.

Synopsis
~~~~~~~~

::

  [DETACH] DELETE expression

Description
~~~~~~~~~~~

*TODO*

Expressions
-----------

Operator Precedence
~~~~~~~~~~~~~~~~~~~

+------------+-----------------+------------------------------+---------------+
| Precedence | Operator        | Description                  | Associativity |
+============+=================+==============================+===============+
| 1          | ``.``           | Property access              | Left-to-right |
+------------+-----------------+------------------------------+               |
| 2          | ``[]``          | Map and list subscripting    |               |
|            +-----------------+------------------------------+               |
|            | ``()``          | Function call                |               |
+------------+-----------------+------------------------------+---------------+
| 3          | ``STARTS WITH`` | Case-sensitive prefix        | None          |
|            |                 | searching on strings         |               |
|            +-----------------+------------------------------+               |
|            | ``ENDS WITH``   | Case-sensitive suffix        |               |
|            |                 | searching on strings         |               |
|            +-----------------+------------------------------+               |
|            | ``CONTAINS``    | Case-sensitive inclusion     |               |
|            |                 | searching on strings         |               |
+------------+-----------------+------------------------------+---------------+
| 4          | ``-``           | Unary minus                  | Right-to-left |
+------------+-----------------+------------------------------+---------------+
| 5          | ``IN``          | Checking if an element       | None          |
|            |                 | exists in a list             |               |
|            +-----------------+------------------------------+               |
|            | ``IS NULL``     | Checking a value is NULL     |               |
|            +-----------------+------------------------------+               |
|            | ``IS NOT NULL`` | Checking a value is not NULL |               |
+------------+-----------------+------------------------------+---------------+
| 6          | ``^``           | Exponentiation               | Left-to-right |
+------------+-----------------+------------------------------+               |
| 7          | ``*`` ``/``     | Multiplication, division,    |               |
|            | ``%``           | and remainder                |               |
+------------+-----------------+------------------------------+               |
| 8          | ``+`` ``-``     | Addition and subtraction     |               |
+------------+-----------------+------------------------------+---------------+
| 9          | ``=`` ``<>``    | For relational = and ≠       | None          |
|            |                 | respectively                 |               |
|            +-----------------+------------------------------+               |
|            | ``<`` ``<=``    | For relational operators <   |               |
|            |                 | and ≤ respectively           |               |
|            +-----------------+------------------------------+               |
|            | ``>`` ``>=``    | For relational operators >   |               |
|            |                 | and ≥ respectively           |               |
+------------+-----------------+------------------------------+---------------+
| 10         | ``NOT``         | Logical NOT                  | Right-to-left |
+------------+-----------------+------------------------------+---------------+
| 11         | ``AND``         | Logical AND                  | Left-to-right |
+------------+-----------------+------------------------------+               |
| 12         | ``OR``          | Logical OR                   |               |
+------------+-----------------+------------------------------+---------------+
