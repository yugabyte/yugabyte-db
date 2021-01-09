.. Licensed to the Apache Software Foundation (ASF) under one
   or more contributor license agreements.  See the NOTICE file
   distributed with this work for additional information
   regarding copyright ownership.  The ASF licenses this file
   to you under the Apache License, Version 2.0 (the
   "License"); you may not use this file except in compliance
   with the License.  You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing,
   software distributed under the License is distributed on an
   "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
   KIND, either express or implied.  See the License for the
   specific language governing permissions and limitations
   under the License.

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
