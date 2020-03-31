Cypher Query Language
=====================

MATCH
-----

Searches for the pattern described in it.

Synopsis
~~~~~~~~

::

  MATCH { node [ relationship node ] [ ... ] } [, ...]

Description
~~~~~~~~~~~

  *The current implementation takes only one node as the pattern.*

RETURN
------

Defines what to include in the query's result set.

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

  *Aggregation is not supported yet.*

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

If ``expression`` is a variable, ``alias`` can be omitted. Otherwise, ``alias`` is required.

  *Aggregation is not supported yet.*

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

An optional sub-clause following ``RETURN`` or ``WITH`` that specifies how the output should be sorted.

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

``start`` is an expression whose evaluated value is an integer.

LIMIT
-----

Constrains the number of records in the output.

Synopsis
~~~~~~~~

::

  LIMIT count

Description
~~~~~~~~~~~

``count`` is an expression whose evaluated value is an integer.

CREATE
------

Creates nodes and relationships.

Synopsis
~~~~~~~~

::

  CREATE { node [ relationship node ] [ ... ] } [, ...]

Description
~~~~~~~~~~~

  *The current implementation takes only one node as the pattern.*

DELETE
------

Deletes nodes and relationships from the graph. Use ``DETACH`` to delete a node and any relationship it has.

Synopsis
~~~~~~~~

::

  [DETACH] DELETE { node | edge }, ...

Description
~~~~~~~~~~~

  *This clause is not supported yet.*

SET
---

Sets and updates properties from nodes and expressions given an expression.

Synopsis
~~~~~~~~

::

  SET expression = expression [, ...]

Description
~~~~~~~~~~~

  *This clause is not supported yet.*

REMOVE
------

Removes properties from nodes and relationships.

Synopsis
~~~~~~~~

::

  REMOVE expression, ...

Description
~~~~~~~~~~~

  *This clause is not supported yet.*

Expressions
-----------

Constants
~~~~~~~~~

String
""""""

Both ``'single-quoted'`` and ``"double-quoted"`` string formats are supported. Only UTF-8 encoding is allowed.

The folowing escape sequences are defined. Other escape sequences will cause a parse error.

+-----------------+-----------------------------------------------------+
| Escape sequence | Character represented                               |
+=================+=====================================================+
| ``\b``          | Backspace                                           |
+-----------------+-----------------------------------------------------+
| ``\f``          | Formfeed Page Break                                 |
+-----------------+-----------------------------------------------------+
| ``\n``          | Newline (Line Feed)                                 |
+-----------------+-----------------------------------------------------+
| ``\r``          | Carriage Return                                     |
+-----------------+-----------------------------------------------------+
| ``\t``          | Horizontal Tab                                      |
+-----------------+-----------------------------------------------------+
| ``\/``          | Slash (optional)                                    |
+-----------------+-----------------------------------------------------+
| ``\\``          | Backslash                                           |
+-----------------+-----------------------------------------------------+
| ``\'``          | Single quotation mark                               |
+-----------------+-----------------------------------------------------+
| ``\"``          | Double quotation mark                               |
+-----------------+-----------------------------------------------------+
| ``\uhhhh``      | Unicode code point below 10000 hexadecimal          |
+-----------------+-----------------------------------------------------+
| ``\Uhhhhhhhh``  | Unicode code point where ``h`` is hexadecimal digit |
+-----------------+-----------------------------------------------------+

Integer
"""""""

|project| uses 64-bit integer.

Float
"""""

|project| stores floating-point numbers in IEEE 754 binary64 format.

It supports the following formats along with scientific notation.

- ``0.``
- ``.0``

Also, the following values are supported.

- ``NaN``
- ``Infinity``
- ``-Infinity``
- ``inf``
- ``-inf``

Boolean
"""""""

``true`` and ``false``

Null
""""

``null``

List
~~~~

A list is an ordered collection of values. It can be built with list literal syntax as shown below.

::

  '[' expression [, ...] ']'

The following simple example shows how a list value is to be built using the above syntax.

.. code-block:: psql

  =# SELECT * FROM cypher('g', $$
  $# RETURN [7, .7, true, null, ['nested'], {p: 'nested'}]
  $# $$) AS (list agtype);
                         list
  ---------------------------------------------------
   [7, 0.7, true, null, ["nested"], {"p": "nested"}]
  (1 row)

Map
~~~

A map is a collection of key/value pairs. It can be built with map literal syntax as shown below.

::

  '{' ( identifier : expression ) [, ...] ']'

The following simple example shows how a map value is to be built using the above syntax.

.. code-block:: psql

  =# SELECT * FROM cypher('g', $$
  $# RETURN {i: 7, f: .7, b: true, z: null, l: ['nested'], m: {p: 'nested'}}
  $# $$) AS (map agtype);
                                         map
  ---------------------------------------------------------------------------------
   {"b": true, "f": 0.7, "i": 7, "l": ["nested"], "m": {"p": "nested"}, "z": null}
  (1 row)

Variables
~~~~~~~~~

All valid identifiers can be variable names except reserved keywords. (See :ref:`get_cypher_keywords`)

An identifier starts with an alphabet (``A-Z`` and ``a-z``) or an underscore (``_``) and can contain alphabets, underscores, dollar-signs (``$``), and digits (``0-9``).

```Backquote-quoted``` identifiers can have any character.

Parameters
~~~~~~~~~~

A parameter starts with a dollar-sign (``$``) followed by an identifier. For example, ``$id`` is a valid parameter.

Parameters are passed as a map to ``cypher()`` function call as the third argument. For example, if the map has a key/value pair whose key is ``"id"`` and value is ``0``, the ``$id`` parameter in the query will be replaced with ``0``.

Operators
~~~~~~~~~

Mathematical Operators
""""""""""""""""""""""

- ``+``
- ``-`` (subtraction or unary minus)
- ``*``
- ``/``
- ``%``
- ``^``

Comparison Operators
""""""""""""""""""""

- ``=``
- ``<>``
- ``<``
- ``<=``
- ``>``
- ``>=``
- ``IS NULL``
- ``IS NOT NULL``

String-specific Comparison Operators
""""""""""""""""""""""""""""""""""""

- ``STARTS WITH``
- ``ENDS WITH``
- ``CONTAINS``

Boolean Operators
"""""""""""""""""

- ``AND``
- ``OR``
- ``NOT``

List Operators
""""""""""""""

- ``[]``
- ``[..]`` (slicing)
- ``+`` (concatenation)
- ``IN``

Map Operators
"""""""""""""

- ``.``
- ``[]``

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

Functions
---------

*TODO*

Comments
--------

|project| supports both "C-style" multi-line and "C++-style" single-line commenting formats as shown below.

.. code-block:: cpp

  /*
   * "C-style"
   * multi-line
   * comment
   */

  // "C++-style" single-line comment
