# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
# http://www.apache.org/licenses/LICENSE-2.0
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.

from age import *
import psycopg2
import networkx as nx
from psycopg2 import sql
from psycopg2.extras import execute_values
from typing import Dict, Any, List, Set
from age.models import Vertex, Edge, Path


def checkIfGraphNameExistInAGE(connection: psycopg2.connect,
                               graphName: str):
    """Check if the age graph exists"""
    with connection.cursor() as cursor:
        cursor.execute(sql.SQL("""
                    SELECT count(*) 
                    FROM ag_catalog.ag_graph 
                    WHERE name='%s'
                """ % (graphName)))
        if cursor.fetchone()[0] == 0:
            raise GraphNotFound(graphName)


def getOidOfGraph(connection: psycopg2.connect,
                  graphName: str) -> int:
    """Returns oid of a graph"""
    try:
        with connection.cursor() as cursor:
            cursor.execute(sql.SQL("""
                        SELECT graphid FROM ag_catalog.ag_graph WHERE name='%s' ;
                    """ % (graphName)))
            oid = cursor.fetchone()[0]
            return oid
    except Exception as e:
        print(e)


def get_vlabel(connection: psycopg2.connect,
               graphName: str):
    node_label_list = []
    oid = getOidOfGraph(connection, graphName)
    try:
        with connection.cursor() as cursor:
            cursor.execute(
                """SELECT name FROM ag_catalog.ag_label WHERE kind='v' AND graph=%s;""" % oid)
            for row in cursor:
                node_label_list.append(row[0])

    except Exception as ex:
        print(type(ex), ex)
    return node_label_list


def create_vlabel(connection: psycopg2.connect,
                  graphName: str,
                  node_label_list: List):
    """create_vlabels from list if not exist"""
    try:
        node_label_set = set(get_vlabel(connection, graphName))
        crete_label_statement = ''
        for label in node_label_list:
            if label in node_label_set:
                continue
            crete_label_statement += """SELECT create_vlabel('%s','%s');\n""" % (
                graphName, label)
        if crete_label_statement != '':
            with connection.cursor() as cursor:
                cursor.execute(crete_label_statement)
                connection.commit()
    except Exception as e:
        raise Exception(e)


def get_elabel(connection: psycopg2.connect,
               graphName: str):
    edge_label_list = []
    oid = getOidOfGraph(connection, graphName)
    try:
        with connection.cursor() as cursor:
            cursor.execute(
                """SELECT name FROM ag_catalog.ag_label WHERE kind='e' AND graph=%s;""" % oid)
            for row in cursor:
                edge_label_list.append(row[0])
    except Exception as ex:
        print(type(ex), ex)
    return edge_label_list


def create_elabel(connection: psycopg2.connect,
                  graphName: str,
                  edge_label_list: List):
    """create_vlabels from list if not exist"""
    try:
        edge_label_set = set(get_elabel(connection, graphName))
        crete_label_statement = ''
        for label in edge_label_list:
            if label in edge_label_set:
                continue
            crete_label_statement += """SELECT create_elabel('%s','%s');\n""" % (
                graphName, label)
        if crete_label_statement != '':
            with connection.cursor() as cursor:
                cursor.execute(crete_label_statement)
                connection.commit()
    except Exception as e:
        raise Exception(e)


def getNodeLabelListAfterPreprocessing(G: nx.DiGraph):
    """
        - Add default label if label is missing 
        - Add properties if not exist 
        - return all distinct node label
    """
    node_label_list = set()
    try:
        for node, data in G.nodes(data=True):
            if 'label' not in data:
                data['label'] = '_ag_label_vertex'
            if 'properties' not in data:
                data['properties'] = {}
            if not isinstance(data['label'], str):
                raise Exception(f"label of node : {node} must be a string")
            if not isinstance(data['properties'], Dict):
                raise Exception(f"properties of node : {node} must be a dict")
            if '__id__' not in data['properties'].keys():
                data['properties']['__id__'] = node
            node_label_list.add(data['label'])
    except Exception as e:
        raise Exception(e)
    return node_label_list


def getEdgeLabelListAfterPreprocessing(G: nx.DiGraph):
    """
        - Add default label if label is missing 
        - Add properties if not exist 
        - return all distinct edge label
    """
    edge_label_list = set()
    try:
        for u, v, data in G.edges(data=True):
            if 'label' not in data:
                data['label'] = '_ag_label_edge'
            if 'properties' not in data:
                data['properties'] = {}
            if not isinstance(data['label'], str):
                raise Exception(f"label of edge : {u}->{v} must be a string")
            if not isinstance(data['properties'], Dict):
                raise Exception(
                    f"properties of edge : {u}->{v} must be a dict")
            edge_label_list.add(data['label'])
    except Exception as e:
        raise Exception(e)
    return edge_label_list


def addAllNodesIntoAGE(connection: psycopg2.connect, graphName: str, G: nx.DiGraph, node_label_list: Set):
    """Add all node to AGE"""
    try:
        queue_data = {label: [] for label in node_label_list}
        id_data = {}

        for node, data in G.nodes(data=True):
            json_string = json.dumps(data['properties'])
            queue_data[data['label']].append((json_string,))

        for label, rows in queue_data.items():
            table_name = """%s."%s" """ % (graphName, label)
            insert_query = f"INSERT INTO {table_name} (properties) VALUES %s RETURNING id"
            cursor = connection.cursor()
            id_data[label] = execute_values(
                cursor, insert_query, rows, fetch=True)
            connection.commit()
            cursor.close()
            id_data[label].reverse()

        for node, data in G.nodes(data=True):
            data['properties']['__gid__'] = id_data[data['label']][-1][0]
            id_data[data['label']].pop()

    except Exception as e:
        raise Exception(e)


def addAllEdgesIntoAGE(connection: psycopg2.connect, graphName: str, G: nx.DiGraph, edge_label_list: Set):
    """Add all edge to AGE"""
    try:
        queue_data = {label: [] for label in edge_label_list}
        for u, v, data in G.edges(data=True):
            json_string = json.dumps(data['properties'])
            queue_data[data['label']].append(
                (G.nodes[u]['properties']['__gid__'], G.nodes[v]['properties']['__gid__'], json_string,))

        for label, rows in queue_data.items():
            table_name = """%s."%s" """ % (graphName, label)
            insert_query = f"INSERT INTO {table_name} (start_id,end_id,properties) VALUES %s"
            cursor = connection.cursor()
            execute_values(cursor, insert_query, rows)
            connection.commit()
            cursor.close()
    except Exception as e:
        raise Exception(e)


def addAllNodesIntoNetworkx(connection: psycopg2.connect, graphName: str, G: nx.DiGraph):
    """Add all nodes to Networkx"""
    node_label_list = get_vlabel(connection, graphName)
    try:
        for label in node_label_list:
            with connection.cursor() as cursor:
                cursor.execute("""
                SELECT id, CAST(properties AS VARCHAR) 
                FROM %s."%s";
                """ % (graphName, label))
                rows = cursor.fetchall()
                for row in rows:
                    G.add_node(int(row[0]), label=label,
                               properties=json.loads(row[1]))
    except Exception as e:
        print(e)


def addAllEdgesIntoNetworkx(connection: psycopg2.connect, graphName: str, G: nx.DiGraph):
    """Add All edges to Networkx"""
    try:
        edge_label_list = get_elabel(connection, graphName)
        for label in edge_label_list:
            with connection.cursor() as cursor:
                cursor.execute("""
                               SELECT start_id, end_id, CAST(properties AS VARCHAR) 
                               FROM %s."%s";
                               """ % (graphName, label))
                rows = cursor.fetchall()
                for row in rows:
                    G.add_edge(int(row[0]), int(
                        row[1]), label=label, properties=json.loads(row[2]))
    except Exception as e:
        print(e)
