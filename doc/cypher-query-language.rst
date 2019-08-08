Cypher Query Language
=====================

``RETURN``
----------

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

``WITH``
--------

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

``WHERE``
---------

Adds constraints to the patterns in a ``MATCH`` or ``OPTIONAL MATCH`` clause or filters the results of a ``WITH`` clause.

Synopsis
~~~~~~~~

::

  WHERE expression

Description
~~~~~~~~~~~

*TODO*

``ORDER BY``
------------

Is a sub-clause following ``RETURN`` or ``WITH``, and it specifies that the output should be sorted and how.

Synopsis
~~~~~~~~

::

  ORDER BY expression [ ASC | ASCENDING | DESC | DESCENDING ] [, ...]

Description
~~~~~~~~~~~

*TODO*

``SKIP``
--------

Defines from which record to start including the records in the output.

Synopsis
~~~~~~~~

::

  SKIP start

Description
~~~~~~~~~~~

*TODO*

``LIMIT``
---------

Constrains the number of records in the output.

Synopsis
~~~~~~~~

::

  LIMIT count

Description
~~~~~~~~~~~

*TODO*
