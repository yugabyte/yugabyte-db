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

import json
from io import StringIO 


TP_NONE = 0
TP_VERTEX = 1
TP_EDGE = 2
TP_PATH = 3


class Graph():
    def __init__(self, stmt=None) -> None:
        self.statement = stmt
        self.rows = list()
        self.vertices = dict()

    def __iter__(self):
        return self.rows.__iter__()

    def __len__(self):
        return self.rows.__len__()

    def __getitem__(self,index):
        return self.rows[index]
        
    def size(self):
        return self.rows.__len__()

    def append(self, agObj):
        self.rows.append(agObj)
    
    def getVertices(self):
        return self.vertices
    
    def getVertex(self, id):
        if id in self.vertices:
            return self.vertices[id]
        else:
            return None

class AGObj:
    @property
    def gtype(self):
        return TP_NONE


class Path(AGObj):
    entities = []
    def __init__(self, entities=None) -> None:
        self.entities = entities

    @property
    def gtype(self):
        return TP_PATH

    def __iter__(self):
        return self.entities.__iter__()

    def __len__(self):
        return self.entities.__len__()

    def __getitem__(self,index):
        return self.entities[index]
        
    def size(self):
        return self.entities.__len__()

    def append(self, agObj:AGObj ):
        self.entities.append(agObj)
    
    def __str__(self) -> str:
        return self.toString()

    def __repr__(self) -> str:
        return self.toString()

    def toString(self) -> str: 
        buf = StringIO()
        buf.write("[")
        max = len(self.entities)
        idx = 0
        while idx < max:
            if idx > 0:
                buf.write(",")
            self.entities[idx]._toString(buf)
            idx += 1
        buf.write("]::PATH")

        return buf.getvalue()

    def toJson(self) -> str:
        buf = StringIO()
        buf.write("{\"gtype\": \"path\", \"elements\": [")
        
        max = len(self.entities)
        idx = 0
        while idx < max:
            if idx > 0:
                buf.write(",")
            self.entities[idx]._toJson(buf)
            idx += 1
        buf.write("]}")

        return buf.getvalue()


    

class Vertex(AGObj):
    def __init__(self, id=None, label=None, properties=None) -> None:
        self.id = id
        self.label = label
        self.properties = properties

    @property
    def gtype(self):
        return TP_VERTEX

    def __setitem__(self,name, value):
        self.properties[name]=value
        
    def __getitem__(self,name):
        if name in self.properties:
            return self.properties[name]
        else:
            return None

    def __str__(self) -> str:
        return self.toString()

    def __repr__(self) -> str:
        return self.toString()

    def toString(self) -> str: 
        return nodeToString(self)

    def _toString(self, buf): 
        _nodeToString(self, buf)

    def toJson(self) -> str:
        return nodeToJson(self)

    def _toJson(self, buf): 
        _nodeToJson(self, buf)


class Edge(AGObj):
    def __init__(self, id=None, label=None, properties=None) -> None:
        self.id = id
        self.label = label
        self.start_id = None
        self.end_id = None
        self.properties = properties

    @property
    def gtype(self):
        return TP_EDGE

    def __setitem__(self,name, value):
        self.properties[name]=value
        
    def __getitem__(self,name):
        if name in self.properties:
            return self.properties[name]
        else:
            return None

    def __str__(self) -> str:
        return self.toString()

    def __repr__(self) -> str:
        return self.toString()

    def extraStrFormat(node, buf):
        if node.start_id != None:
            buf.write(", start_id:")
            buf.write(str(node.start_id))

        if node.end_id != None:
            buf.write(", end_id:")
            buf.write(str(node.end_id))


    def toString(self) -> str: 
        return nodeToString(self, Edge.extraStrFormat)

    def _toString(self, buf): 
        _nodeToString(self, buf, Edge.extraStrFormat)

    def extraJsonFormat(node, buf):
        if node.start_id != None:
            buf.write(", \"start_id\": \"")
            buf.write(str(node.start_id))
            buf.write("\"")

        if node.end_id != None:
            buf.write(", \"end_id\": \"")
            buf.write(str(node.end_id))
            buf.write("\"")

    def toJson(self) -> str:
        return nodeToJson(self, Edge.extraJsonFormat)

    def _toJson(self, buf): 
        _nodeToJson(self, buf, Edge.extraJsonFormat)


def nodeToString(node, extraFormatter=None):
    buf = StringIO() 
    _nodeToString(node,buf,extraFormatter=extraFormatter)
    return buf.getvalue()


def _nodeToString(node, buf, extraFormatter=None):
    buf.write("{")
    if node.label != None:
        buf.write("label:")
        buf.write(node.label)
        
    if node.id != None:
        buf.write(", id:")
        buf.write(str(node.id))
        
    if node.properties != None:
        buf.write(", properties:{")
        prop_list = []
        for k, v in node.properties.items():
            prop_list.append(f"{k}: {str(v)}")

        # Join properties with comma and write to buffer
        buf.write(", ".join(prop_list))
        buf.write("}")

    if extraFormatter != None:
        extraFormatter(node, buf)
        
    if node.gtype == TP_VERTEX:
        buf.write("}::VERTEX")
    if node.gtype == TP_EDGE:
        buf.write("}::EDGE")


def nodeToJson(node, extraFormatter=None):
    buf = StringIO()
    _nodeToJson(node, buf, extraFormatter=extraFormatter)
    return buf.getvalue()


def _nodeToJson(node, buf, extraFormatter=None):
    buf.write("{\"gtype\": ")
    if node.gtype == TP_VERTEX:
        buf.write("\"vertex\", ")
    if node.gtype == TP_EDGE:
        buf.write("\"edge\", ")

    if node.label != None:
        buf.write("\"label\":\"")
        buf.write(node.label)
        buf.write("\"")
        
    if node.id != None:
        buf.write(", \"id\":")
        buf.write(str(node.id))
        
    if extraFormatter != None:
        extraFormatter(node, buf)
        
    if node.properties != None:
        buf.write(", \"properties\":{")

        prop_list = []
        for k, v in node.properties.items():
            prop_list.append(f"\"{k}\": \"{str(v)}\"")

        # Join properties with comma and write to buffer
        buf.write(", ".join(prop_list))
        buf.write("}")
    buf.write("}")
    