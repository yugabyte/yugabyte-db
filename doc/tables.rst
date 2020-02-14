Tables
======

.. _ag_graph:

ag_graph
~~~~~~~~

The catalog ``ag_graph`` stores graphs.

+---------------+----------+----------------------+-------------------------+
| Name          | Type     | References           | Description             |
+===============+==========+======================+=========================+
| ``name``      | ``name`` |                      | Name of the graph       |
+---------------+----------+----------------------+-------------------------+
| ``namespace`` | ``oid``  | ``pg_namespace.oid`` | The namespace that      |
|               |          |                      | actually stores data of |
|               |          |                      | the graph               |
+---------------+----------+----------------------+-------------------------+
