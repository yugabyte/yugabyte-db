Tables
======

All tables reside in ``ag_catalog`` schema.

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

.. _ag_label:

ag_label
~~~~~~~~

The catalog ``ag_label`` stores labels.

+--------------+----------------+------------------+------------------------+
| Name         | Type           | References       | Description            |
+==============+================+==================+========================+
| ``name``     | ``name``       |                  | Name of the label      |
+--------------+----------------+------------------+------------------------+
| ``graph``    | ``oid``        | ``ag_graph.oid`` | The graph that this    |
|              |                |                  | label belongs to       |
+--------------+----------------+------------------+------------------------+
| ``id``       | ``label_id``   |                  | Unique ID of the label |
+--------------+----------------+------------------+------------------------+
| ``kind``     | ``label_kind`` |                  | ``'v'`` means vertex   |
|              |                |                  | and ``'e'`` means edge |
+--------------+----------------+------------------+------------------------+
| ``relation`` | ``regclass``   | ``pg_class.oid`` | The relation that      |
|              |                |                  | actually stores        |
|              |                |                  | entries of the label   |
+--------------+----------------+------------------+------------------------+

``label_id`` is a domain type which has ``int`` as its underlying type. It checks that ``id`` is from 1 to 65535 inclusive. Therefore, a graph may have up to 65535 labels regardless of their ``label_kind``.

``label_kind`` is a domain type which has ``char`` as its underlying type. It checks that ``kind`` is either ``'v'`` (vertex) or ``'e'`` (edge).
