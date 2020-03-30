Utility Functions
=================

All utility functions reside in ``ag_catalog`` schema.

create_graph()
--------------

Creates a graph in the database.

Prototype
~~~~~~~~~

``create_graph(graph_name name) void``

Parameters
~~~~~~~~~~

+----------------+----------------------+
| Name           | Description          |
+================+======================+
| ``graph_name`` | The name of a graph. |
+----------------+----------------------+

Return Value
~~~~~~~~~~~~

N/A

Examples
~~~~~~~~

.. code-block:: psql

  =# SELECT create_graph('g');
  NOTICE:  graph "g" has been created
   create_graph
  --------------
  
  (1 row)

drop_graph()
------------

Removes a graph from the database.

Prototype
~~~~~~~~~

``drop_graph(graph_name name, cascade boolean = false) void``

Parameters
~~~~~~~~~~

+----------------+---------------------------------------------------------+
| Name           | Description                                             |
+================+=========================================================+
| ``graph_name`` | The name of a graph.                                    |
+----------------+---------------------------------------------------------+
| ``cascade``    | [optional] Automatically drop objects (labels, indexes, |
|                | etc.) that are contained in the graph.                  |
+----------------+---------------------------------------------------------+

Return Value
~~~~~~~~~~~~

N/A

Examples
~~~~~~~~

.. code-block:: psql

  =# SELECT drop_graph('g');
  NOTICE:  graph "g" has been dropped
   drop_graph
  ------------
  
  (1 row)

alter_graph()
-------------

Alters a graph characteristic. Currently, the only operation supported is ``rename``.

Prototype
~~~~~~~~~

``alter_graph(graph_name name, operation cstring, new_value name) void``

Parameters
~~~~~~~~~~

+----------------+---------------------------------------------------------+
| Name           | Description                                             |
+================+=========================================================+
| ``graph_name`` | The name of the graph to modify. This parameter is case |
|                | sensitive.                                              |
+----------------+---------------------------------------------------------+
| ``operation``  | The name of the operation - see below. This parameter   |
|                | is case insensitive and needs to be in single quotes.   |
|                |                                                         |
|                | ``rename`` - renames ``graph_name`` to ``new_value``.   |
+----------------+---------------------------------------------------------+
| ``new_value``  | The new value. This parameter is case sensitive.        |
+----------------+---------------------------------------------------------+

Return Value
~~~~~~~~~~~~

N/A

Examples
~~~~~~~~

.. code-block:: psql

  =# SELECT alter_graph('Network', 'rename', 'lan_network');
  NOTICE:  graph "Network" renamed to "lan_network"
   alter_graph
  -------------
  
  (1 row)

drop_label()
------------

Drops a label in a graph.

Prototype
~~~~~~~~~

``drop_label(graph_name name, label_name name) void``

Parameters
~~~~~~~~~~

+----------------+----------------------+
| Name           | Description          |
+================+======================+
| ``graph_name`` | The name of a graph. |
+----------------+----------------------+
| ``label_name`` | The name of a label. |
+----------------+----------------------+

Return Value
~~~~~~~~~~~~

N/A

Examples
~~~~~~~~

.. code-block:: psql

  =# SELECT drop_label('g', 'v');
  NOTICE:  label "g"."v" has been dropped
   drop_label
  ------------
  
  (1 row)

.. _get_cypher_keywords:

get_cypher_keywords()
---------------------

Returns the list of keywords in Cypher and their categories.

Prototype
~~~~~~~~~

``get_cypher_keywords() SETOF record``

Parameters
~~~~~~~~~~

N/A

Return Value
~~~~~~~~~~~~

The list of keywords in Cypher and their categories.

Examples
~~~~~~~~

.. code-block:: psql

  =# SELECT * FROM get_cypher_keywords();
      word    | catcode | catdesc
  ------------+---------+----------
   and        | R       | reserved
   as         | R       | reserved
   asc        | R       | reserved
   ascending  | R       | reserved
   by         | R       | reserved
   contains   | R       | reserved
   create     | R       | reserved
   delete     | R       | reserved
   desc       | R       | reserved
   descending | R       | reserved
   detach     | R       | reserved
   distinct   | R       | reserved
   ends       | R       | reserved
   false      | R       | reserved
   in         | R       | reserved
   is         | R       | reserved
   limit      | R       | reserved
   match      | R       | reserved
   not        | R       | reserved
   null       | R       | reserved
   or         | R       | reserved
   order      | R       | reserved
   remove     | R       | reserved
   return     | R       | reserved
   set        | R       | reserved
   skip       | R       | reserved
   starts     | R       | reserved
   true       | R       | reserved
   where      | R       | reserved
   with       | R       | reserved
  (30 rows)
