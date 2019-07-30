Utility Functions
=================

``create_graph``
----------------

``create_graph()`` defines a graph in the database.

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

.. code-block:: postgresql

  =# SELECT create_graph('g');
  NOTICE:  graph "g" has been created
   create_graph
  --------------
  
  (1 row)

``drop_graph``
--------------

``drop_graph()`` removes a graph from the database.

Prototype
~~~~~~~~~

``drop_graph(graph_name name, cascade bool = false) void``

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

.. code-block:: postgresql

  =# SELECT drop_graph('g');
  NOTICE:  graph "g" has been dropped
   drop_graph
  ------------
  
  (1 row)
