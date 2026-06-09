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
import psycopg
import networkx as nx
from age.models import Vertex, Edge, Path
from .lib import *


def age_to_networkx(connection: psycopg.connect,
                    graphName: str,
                    G: None | nx.DiGraph = None,
                    query: str | None = None
                    ) -> nx.DiGraph:
    """
    @params
    ---------------------
    connection - (psycopg.connect) Connection object
    graphName - (str) Name of the graph
    G - (networkx.DiGraph) Networkx directed Graph [optional]
    query - (str) Cypher query [optional]

        @returns
    ------------
    Networkx directed Graph

    """

    # Check if the age graph exists
    checkIfGraphNameExistInAGE(connection, graphName)

    # Create an empty directed graph
    if G == None:
        G = nx.DiGraph()

    def addNodeToNetworkx(node):
        """Add Nodes in Networkx"""
        G.add_node(node.id,
                   label=node.label,
                   properties=node.properties)

    def addEdgeToNetworkx(edge):
        """Add Edge in Networkx"""
        G.add_edge(edge.start_id,
                   edge.end_id,
                   label=edge.label,
                   properties=edge.properties)

    def addPath(path):
        """Add Edge in Networkx"""
        for x in path:
            if (type(x) == Path):
                addPath(x)
        for x in path:
            if (type(x) == Vertex):
                addNodeToNetworkx(x)
        for x in path:
            if (type(x) == Edge):
                addEdgeToNetworkx(x)

    # Setting up connection to work with Graph
    age.setUpAge(connection, graphName)

    if (query == None):
        addAllNodesIntoNetworkx(connection, graphName, G)
        addAllEdgesIntoNetworkx(connection, graphName, G)
    else:
        with connection.cursor() as cursor:
            cursor.execute(query)
            rows = cursor.fetchall()
            for row in rows:
                for x in row:
                    if type(x) == Path:
                        addPath(x)
                    elif type(x) == Edge:
                        addEdgeToNetworkx(x)
                    elif type(x) == Vertex:
                        addNodeToNetworkx(x)
    return G
