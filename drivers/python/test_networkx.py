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

import age
import unittest
import argparse
import networkx as nx
from age.models import *
from age.networkx import *
from age.exceptions import *


TEST_GRAPH_NAME = "test_graph"
ORIGINAL_GRAPH = "original_graph"
EXPECTED_GRAPH = "expected_graph"


class TestAgeToNetworkx(unittest.TestCase):
    ag = None

    def setUp(self):

        TEST_DB = self.args.database
        TEST_USER = self.args.user
        TEST_PASSWORD = self.args.password
        TEST_PORT = self.args.port
        TEST_HOST = self.args.host
        self.ag = age.connect(graph=TEST_GRAPH_NAME, host=TEST_HOST, port=TEST_PORT,
                              dbname=TEST_DB, user=TEST_USER, password=TEST_PASSWORD)

    def tearDown(self):
        age.deleteGraph(self.ag.connection, self.ag.graphName)
        self.ag.close()

    def compare_networkX(self, G, H):
        if G.number_of_nodes() != H.number_of_nodes():
            return False
        if G.number_of_edges() != H.number_of_edges():
            return False
        # test nodes
        nodes_G, nodes_H = G.number_of_nodes(), H.number_of_nodes()
        markG, markH = [0]*nodes_G, [0]*nodes_H
        nodes_list_G, nodes_list_H = list(G.nodes), list(H.nodes)
        for i in range(0, nodes_G):
            for j in range(0, nodes_H):
                if markG[i] == 0 and markH[j] == 0:
                    node_id_G = nodes_list_G[i]
                    property_G = G.nodes[node_id_G]

                    node_id_H = nodes_list_H[j]
                    property_H = H.nodes[node_id_H]
                    if property_G == property_H:
                        markG[i] = 1
                        markH[j] = 1

        if any(elem == 0 for elem in markG):
            return False
        if any(elem == 0 for elem in markH):
            return False

        # test edges
        edges_G, edges_H = G.number_of_edges(), H.number_of_edges()
        markG, markH = [0]*edges_G, [0]*edges_H
        edges_list_G, edges_list_H = list(G.edges), list(H.edges)

        for i in range(0, edges_G):
            for j in range(0, edges_H):
                if markG[i] == 0 and markH[j] == 0:
                    source_G, target_G = edges_list_G[i]
                    property_G = G.edges[source_G, target_G]

                    source_H, target_H = edges_list_H[j]
                    property_H = H.edges[source_H, target_H]

                    if property_G == property_H:
                        markG[i] = 1
                        markH[j] = 1

        if any(elem == 0 for elem in markG):
            return False
        if any(elem == 0 for elem in markH):
            return False

        return True

    def test_empty_graph(self):
        print('Testing AGE to Networkx for empty graph')
        # Expected Graph
        # Empty Graph
        G = nx.DiGraph()

        # Convert Apache AGE to NetworkX
        H = age_to_networkx(self.ag.connection, TEST_GRAPH_NAME)

        self.assertTrue(self.compare_networkX(G, H))

    def test_existing_graph_without_query(self):
        print('Testing AGE to Networkx for non empty graph without query')
        ag = self.ag
        # Create nodes
        ag.execCypher("CREATE (n:Person {name: %s}) ", params=('Jack',))
        ag.execCypher("CREATE (n:Person {name: %s}) ", params=('Andy',))
        ag.execCypher("CREATE (n:Person {name: %s}) ", params=('Smith',))
        ag.commit()

        # Create Edges
        ag.execCypher("""MATCH (a:Person), (b:Person)
                    WHERE a.name = 'Andy' AND b.name = 'Jack'
                    CREATE (a)-[r:workWith {weight: 3}]->(b)""")
        ag.execCypher("""MATCH (a:Person), (b:Person) 
                    WHERE  a.name = %s AND b.name = %s 
                    CREATE p=((a)-[r:workWith {weight: 10}]->(b)) """, params=('Jack', 'Smith',))
        ag.commit()

        G = age_to_networkx(self.ag.connection, TEST_GRAPH_NAME)

        # Check that the G has the expected properties
        self.assertIsInstance(G, nx.DiGraph)

        # Check that the G has the correct number of nodes and edges
        self.assertEqual(len(G.nodes), 3)
        self.assertEqual(len(G.edges), 2)

        # Check that the node properties are correct
        for node in G.nodes:
            self.assertEqual(int, type(node))
            self.assertEqual(G.nodes[node]['label'], 'Person')
            self.assertIn('name', G.nodes[node]['properties'])
            self.assertIn('properties', G.nodes[node])
            self.assertEqual(str, type(G.nodes[node]['label']))

        # Check that the edge properties are correct
        for edge in G.edges:
            self.assertEqual(tuple, type(edge))
            self.assertEqual(int, type(edge[0]) and type(edge[1]))
            self.assertEqual(G.edges[edge]['label'], 'workWith')
            self.assertIn('weight', G.edges[edge]['properties'])
            self.assertEqual(int, type(G.edges[edge]['properties']['weight']))

    def test_existing_graph_with_query(self):
        print('Testing AGE to Networkx for non empty graph with query')

        ag = self.ag
        # Create nodes
        ag.execCypher("CREATE (n:Person {name: %s}) ", params=('Jack',))
        ag.execCypher("CREATE (n:Person {name: %s}) ", params=('Andy',))
        ag.execCypher("CREATE (n:Person {name: %s}) ", params=('Smith',))
        ag.commit()

        # Create Edges
        ag.execCypher("""MATCH (a:Person), (b:Person)
                    WHERE a.name = 'Andy' AND b.name = 'Jack'
                    CREATE (a)-[r:workWith {weight: 3}]->(b)""")
        ag.execCypher("""MATCH (a:Person), (b:Person) 
                    WHERE  a.name = %s AND b.name = %s 
                    CREATE p=((a)-[r:workWith {weight: 10}]->(b)) """, params=('Jack', 'Smith',))
        ag.commit()

        query = """SELECT * FROM cypher('%s', $$ MATCH (a:Person)-[r:workWith]->(b:Person)
        WHERE a.name = 'Andy'
        RETURN a, r, b $$) AS (a agtype, r agtype, b agtype);
        """ % (TEST_GRAPH_NAME)

        G = age_to_networkx(self.ag.connection,
                            graphName=TEST_GRAPH_NAME, query=query)

        # Check that the G has the expected properties
        self.assertIsInstance(G, nx.DiGraph)

        # Check that the G has the correct number of nodes and edges
        self.assertEqual(len(G.nodes), 2)
        self.assertEqual(len(G.edges), 1)

        # Check that the node properties are correct
        for node in G.nodes:
            self.assertEqual(int, type(node))
            self.assertEqual(G.nodes[node]['label'], 'Person')
            self.assertIn('name', G.nodes[node]['properties'])
            self.assertIn('properties', G.nodes[node])
            self.assertEqual(str, type(G.nodes[node]['label']))

        # Check that the edge properties are correct
        for edge in G.edges:
            self.assertEqual(tuple, type(edge))
            self.assertEqual(int, type(edge[0]) and type(edge[1]))
            self.assertEqual(G.edges[edge]['label'], 'workWith')
            self.assertIn('weight', G.edges[edge]['properties'])
            self.assertEqual(int, type(G.edges[edge]['properties']['weight']))

    def test_existing_graph(self):
        print("Testing AGE to NetworkX for non-existing graph")
        ag = self.ag

        non_existing_graph = "non_existing_graph"
        # Check that the function raises an exception for non existing graph
        with self.assertRaises(GraphNotFound) as context:
            age_to_networkx(ag.connection, graphName=non_existing_graph)
        # Check the raised exception has the expected error message
        self.assertEqual(str(context.exception), non_existing_graph)


class TestNetworkxToAGE(unittest.TestCase):
    ag = None
    ag1 = None
    ag2 = None

    def setUp(self):
        TEST_DB = self.args.database
        TEST_USER = self.args.user
        TEST_PASSWORD = self.args.password
        TEST_PORT = self.args.port
        TEST_HOST = self.args.host
        self.ag = age.connect(graph=TEST_GRAPH_NAME, host=TEST_HOST, port=TEST_PORT,
                              dbname=TEST_DB, user=TEST_USER, password=TEST_PASSWORD)
        self.ag1 = age.connect(graph=ORIGINAL_GRAPH, host=TEST_HOST, port=TEST_PORT,
                               dbname=TEST_DB, user=TEST_USER, password=TEST_PASSWORD)
        self.ag2 = age.connect(graph=EXPECTED_GRAPH, host=TEST_HOST, port=TEST_PORT,
                               dbname=TEST_DB, user=TEST_USER, password=TEST_PASSWORD)
        self.graph = nx.DiGraph()

    def tearDown(self):
        age.deleteGraph(self.ag1.connection, self.ag1.graphName)
        age.deleteGraph(self.ag2.connection, self.ag2.graphName)
        age.deleteGraph(self.ag.connection, self.ag.graphName)
        self.ag.close()
        self.ag1.close()
        self.ag2.close()

    def compare_age(self, age1, age2):
        cursor = age1.execCypher("MATCH (v) RETURN v")
        g_nodes = cursor.fetchall()

        cursor = age1.execCypher("MATCH ()-[r]->() RETURN r")
        g_edges = cursor.fetchall()

        cursor = age2.execCypher("MATCH (v) RETURN v")
        h_nodes = cursor.fetchall()

        cursor = age2.execCypher("MATCH ()-[r]->() RETURN r")
        h_edges = cursor.fetchall()

        if len(g_nodes) != len(h_nodes) or len(g_edges) != len(h_edges):
            return False

        # test nodes
        nodes_G, nodes_H = len(g_nodes), len(h_nodes)
        markG, markH = [0]*nodes_G, [0]*nodes_H

        # return True
        for i in range(0, nodes_G):
            for j in range(0, nodes_H):
                if markG[i] == 0 and markH[j] == 0:
                    property_G = g_nodes[i][0].properties
                    property_G['label'] = g_nodes[i][0].label
                    property_G.pop('__id__')

                    property_H = h_nodes[j][0].properties
                    property_H['label'] = h_nodes[j][0].label

                    if property_G == property_H:
                        markG[i] = 1
                        markH[j] = 1

        if any(elem == 0 for elem in markG):
            return False
        if any(elem == 0 for elem in markH):
            return False

        # test edges
        edges_G, edges_H = len(g_edges), len(h_edges)
        markG, markH = [0]*edges_G, [0]*edges_H

        for i in range(0, edges_G):
            for j in range(0, edges_H):
                if markG[i] == 0 and markH[j] == 0:
                    property_G = g_edges[i][0].properties
                    property_G['label'] = g_edges[i][0].label

                    property_H = h_edges[j][0].properties
                    property_H['label'] = h_edges[j][0].label
                    if property_G == property_H:
                        markG[i] = 1
                        markH[j] = 1

        if any(elem == 0 for elem in markG):
            return False
        if any(elem == 0 for elem in markH):
            return False

        return True

    def test_number_of_nodes_and_edges(self):
        print("Testing Networkx To AGE for number of nodes and edges")
        ag = self.ag

        # Create NetworkX graph
        self.graph.add_node(1, label='Person', properties={
                            'name': 'Moontasir', 'age': '26', 'id': 1})
        self.graph.add_node(2, label='Person', properties={
                            'name': 'Austen', 'age': '26', 'id': 2})
        self.graph.add_edge(1, 2, label='KNOWS', properties={
                            'since': '1997', 'start_id': 1, 'end_id': 2})
        self.graph.add_node(3, label='Person', properties={
                            'name': 'Eric', 'age': '28', 'id': 3})

        networkx_to_age(self.ag.connection, self.graph, TEST_GRAPH_NAME)

        # Check that node(s) were created
        cursor = ag.execCypher('MATCH (n) RETURN n')
        result = cursor.fetchall()
        # Check number of vertices created
        self.assertEqual(len(result), 3)
        # Checks if type of property in query output is a Vertex
        self.assertEqual(Vertex, type(result[0][0]))
        self.assertEqual(Vertex, type(result[1][0]))

        # Check that edge(s) was created
        cursor = ag.execCypher('MATCH ()-[e]->() RETURN e')
        result = cursor.fetchall()
        # Check number of edge(s) created
        self.assertEqual(len(result), 1)
        # Checks if type of property in query output is an Edge
        self.assertEqual(Edge, type(result[0][0]))

    def test_empty_graph(self):
        print("Testing Networkx To AGE for empty Graph")
        # Expected Graph
        # Empty Graph

        # NetworkX Graph
        G = nx.DiGraph()

        # Convert Apache AGE to NetworkX
        networkx_to_age(self.ag1.connection, G, ORIGINAL_GRAPH)

        self.assertTrue(self.compare_age(self.ag1, self.ag2))

    def test_non_empty_graph(self):
        print("Testing Networkx To AGE for non-empty Graph")
        # Expected Graph
        self.ag2.execCypher("CREATE (:l1 {name: 'n1', weight: '5'})")
        self.ag2.execCypher("CREATE (:l1 {name: 'n2', weight: '4'})")
        self.ag2.execCypher("CREATE (:l1 {name: 'n3', weight: '9'})")

        self.ag2.execCypher("""MATCH (a:l1), (b:l1)
                            WHERE a.name = 'n1' AND b.name = 'n2'
                            CREATE (a)-[e:e1 {property:'graph'}]->(b)""")
        self.ag2.execCypher("""MATCH (a:l1), (b:l1)
                            WHERE a.name = 'n2' AND b.name = 'n3'
                            CREATE (a)-[e:e2 {property:'node'}]->(b)""")

        # NetworkX Graph
        G = nx.DiGraph()

        G.add_node('1',
                   label='l1',
                   properties={'name': 'n1',
                               'weight': '5'})
        G.add_node('2',
                   label='l1',
                   properties={'name': 'n2',
                               'weight': '4'})
        G.add_node('3',
                   label='l1',
                   properties={'name': 'n3',
                               'weight': '9'})
        G.add_edge('1', '2', label='e1', properties={'property': 'graph'})
        G.add_edge('2', '3', label='e2', properties={'property': 'node'})

        # Convert Apache AGE to NetworkX
        networkx_to_age(self.ag1.connection, G, ORIGINAL_GRAPH)

        self.assertTrue(self.compare_age(self.ag1, self.ag2))

    def test_invalid_node_label(self):
        print("Testing Networkx To AGE for invalid node label")
        self.graph.add_node(4, label=123, properties={
                            'name': 'Mason', 'age': '24', 'id': 4})

        # Check that the function raises an exception for the invalid node label
        with self.assertRaises(Exception) as context:
            networkx_to_age(self.ag.connection, G=self.graph,
                            graphName=TEST_GRAPH_NAME)
        # Check the raised exception has the expected error message
        self.assertEqual(str(context.exception),
                         "label of node : 4 must be a string")

    def test_invalid_node_properties(self):
        print("Testing Networkx To AGE for invalid node properties")
        self.graph.add_node(4, label='Person', properties="invalid")

        # Check that the function raises an exception for the invalid node properties
        with self.assertRaises(Exception) as context:
            networkx_to_age(self.ag.connection, G=self.graph,
                            graphName=TEST_GRAPH_NAME)
        # Check the raised exception has the expected error message
        self.assertEqual(str(context.exception),
                         "properties of node : 4 must be a dict")

    def test_invalid_edge_label(self):
        print("Testing Networkx To AGE for invalid edge label")
        self.graph.add_edge(1, 2, label=123, properties={
                            'since': '1997', 'start_id': 1, 'end_id': 2})

        # Check that the function raises an exception for the invalid edge label
        with self.assertRaises(Exception) as context:
            networkx_to_age(self.ag.connection, G=self.graph,
                            graphName=TEST_GRAPH_NAME)
        # Check the raised exception has the expected error message
        self.assertEqual(str(context.exception),
                         "label of edge : 1->2 must be a string")

    def test_invalid_edge_properties(self):
        print("Testing Networkx To AGE for invalid edge properties")
        self.graph.add_edge(1, 2, label='KNOWS', properties="invalid")

        # Check that the function raises an exception for the invalid edge properties
        with self.assertRaises(Exception) as context:
            networkx_to_age(self.ag.connection, G=self.graph,
                            graphName=TEST_GRAPH_NAME)
        # Check the raised exception has the expected error message
        self.assertEqual(str(context.exception),
                         "properties of edge : 1->2 must be a dict")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()

    parser.add_argument('-host',
                        '--host',
                        help='Optional Host Name. Default Host is "127.0.0.1" ',
                        default="127.0.0.1")
    parser.add_argument('-port',
                        '--port',
                        help='Optional Port Number. Default port no is 5432',
                        default=5432)
    parser.add_argument('-db',
                        '--database',
                        help='Required Database Name',
                        required=True)
    parser.add_argument('-u',
                        '--user',
                        help='Required Username Name',
                        required=True)
    parser.add_argument('-pass',
                        '--password',
                        help='Required Password for authentication',
                        required=True)

    args = parser.parse_args()
    suite = unittest.TestSuite()

    suite.addTest(TestAgeToNetworkx('test_empty_graph'))
    suite.addTest(TestAgeToNetworkx('test_existing_graph_without_query'))
    suite.addTest(TestAgeToNetworkx('test_existing_graph_with_query'))
    suite.addTest(TestAgeToNetworkx('test_existing_graph'))
    TestAgeToNetworkx.args = args

    suite.addTest(TestNetworkxToAGE('test_number_of_nodes_and_edges'))
    suite.addTest(TestNetworkxToAGE('test_empty_graph'))
    suite.addTest(TestNetworkxToAGE('test_non_empty_graph'))
    suite.addTest(TestNetworkxToAGE('test_invalid_node_label'))
    suite.addTest(TestNetworkxToAGE('test_invalid_node_properties'))
    suite.addTest(TestNetworkxToAGE('test_invalid_edge_label'))
    suite.addTest(TestNetworkxToAGE('test_invalid_edge_properties'))
    TestNetworkxToAGE.args = args

    unittest.TextTestRunner().run(suite)
