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
from .lib import *


def networkx_to_age(connection: psycopg.connect,
                    G: nx.DiGraph,
                    graphName: str):
    """
    @params
    -----------
    connection - (psycopg.connect) Connection object

    G - (networkx.DiGraph) Networkx directed Graph

    graphName - (str) Name of the graph

    @returns
    ------------
    None

    """
    node_label_list = getNodeLabelListAfterPreprocessing(G)
    edge_label_list = getEdgeLabelListAfterPreprocessing(G)

    # Setup connection with Graph
    age.setUpAge(connection, graphName)

    create_vlabel(connection, graphName, node_label_list)
    create_elabel(connection, graphName, edge_label_list)

    addAllNodesIntoAGE(connection, graphName, G, node_label_list)
    addAllEdgesIntoAGE(connection, graphName, G, edge_label_list)
