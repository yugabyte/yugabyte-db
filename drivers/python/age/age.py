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

import re
import psycopg
from psycopg.types import TypeInfo
from psycopg.adapt import Loader
from psycopg import sql
from psycopg.client_cursor import ClientCursor
from .exceptions import *
from .builder import parseAgeValue


_EXCEPTION_NoConnection = NoConnection()
_EXCEPTION_GraphNotSet = GraphNotSet()

WHITESPACE = re.compile('\s')


class AgeDumper(psycopg.adapt.Dumper):
    def dump(self, obj: Any) -> bytes | bytearray | memoryview:
        pass    
    
    
class AgeLoader(psycopg.adapt.Loader):    
    def load(self, data: bytes | bytearray | memoryview) -> Any | None:    
        return parseAgeValue(data.decode('utf-8'))


def setUpAge(conn:psycopg.connection, graphName:str):
    with conn.cursor() as cursor:
        cursor.execute("LOAD 'age';")
        cursor.execute("SET search_path = ag_catalog, '$user', public;")

        ag_info = TypeInfo.fetch(conn, 'agtype')

        if not ag_info:
            raise AgeNotSet()
    
        conn.adapters.register_loader(ag_info.oid, AgeLoader)
        conn.adapters.register_loader(ag_info.array_oid, AgeLoader)

        # Check graph exists
        if graphName != None:
            checkGraphCreated(conn, graphName)

# Create the graph, if it does not exist
def checkGraphCreated(conn:psycopg.connection, graphName:str):
    with conn.cursor() as cursor:
        cursor.execute(sql.SQL("SELECT count(*) FROM ag_graph WHERE name={graphName}").format(graphName=sql.Literal(graphName)))
        if cursor.fetchone()[0] == 0:
            cursor.execute(sql.SQL("SELECT create_graph({graphName});").format(graphName=sql.Literal(graphName)))
            conn.commit()


def deleteGraph(conn:psycopg.connection, graphName:str):
    with conn.cursor() as cursor:
        cursor.execute(sql.SQL("SELECT drop_graph({graphName}, true);").format(graphName=sql.Literal(graphName)))
        conn.commit()


def buildCypher(graphName:str, cypherStmt:str, columns:list) ->str:
    if graphName == None:
        raise _EXCEPTION_GraphNotSet
    
    columnExp=[]
    if columns != None and len(columns) > 0:
        for col in columns:
            if col.strip() == '':
                continue
            elif WHITESPACE.search(col) != None:
                columnExp.append(col)
            else:
                columnExp.append(col + " agtype")
    else:
        columnExp.append('v agtype')

    stmtArr = []
    stmtArr.append("SELECT * from cypher(NULL,NULL) as (")
    stmtArr.append(','.join(columnExp))
    stmtArr.append(");")
    return "".join(stmtArr)

def execSql(conn:psycopg.connection, stmt:str, commit:bool=False, params:tuple=None) -> psycopg.cursor :
    if conn == None or conn.closed:
        raise _EXCEPTION_NoConnection
    
    cursor = conn.cursor()
    try:
        cursor.execute(stmt, params)
        if commit:
            conn.commit()

        return cursor
    except SyntaxError as cause:
        conn.rollback()
        raise cause
    except Exception as cause:
        conn.rollback()
        raise SqlExecutionError("Execution ERR[" + str(cause) +"](" + stmt +")", cause)


def querySql(conn:psycopg.connection, stmt:str, params:tuple=None) -> psycopg.cursor :
    return execSql(conn, stmt, False, params)

# Execute cypher statement and return cursor.
# If cypher statement changes data (create, set, remove),
# You must commit session(ag.commit())
# (Otherwise the execution cannot make any effect.)
def execCypher(conn:psycopg.connection, graphName:str, cypherStmt:str, cols:list=None, params:tuple=None) -> psycopg.cursor :
    if conn == None or conn.closed:
        raise _EXCEPTION_NoConnection

    cursor = conn.cursor()
    #clean up the string for mogrification
    cypherStmt = cypherStmt.replace("\n", "")
    cypherStmt = cypherStmt.replace("\t", "")
    cypher = str(cursor.mogrify(cypherStmt, params))
    cypher = cypher.strip()

    preparedStmt = "SELECT * FROM age_prepare_cypher({graphName},{cypherStmt})"

    cursor = conn.cursor()
    try:
        cursor.execute(sql.SQL(preparedStmt).format(graphName=sql.Literal(graphName),cypherStmt=sql.Literal(cypher)))
    except SyntaxError as cause:
        conn.rollback()
        raise cause
    except Exception as cause:
        conn.rollback()
        raise SqlExecutionError("Execution ERR[" + str(cause) +"](" + preparedStmt +")", cause)

    stmt = buildCypher(graphName, cypher, cols)

    cursor = conn.cursor()
    try:
        cursor.execute(stmt)
        return cursor
    except SyntaxError as cause:
        conn.rollback()
        raise cause
    except Exception as cause:
        conn.rollback()
        raise SqlExecutionError("Execution ERR[" + str(cause) +"](" + stmt +")", cause)


def cypher(cursor:psycopg.cursor, graphName:str, cypherStmt:str, cols:list=None, params:tuple=None) -> psycopg.cursor :
    #clean up the string for mogrification
    cypherStmt = cypherStmt.replace("\n", "")
    cypherStmt = cypherStmt.replace("\t", "")
    cypher = str(cursor.mogrify(cypherStmt, params))
    cypher = cypher.strip()

    preparedStmt = "SELECT * FROM age_prepare_cypher({graphName},{cypherStmt})"
    cursor.execute(sql.SQL(preparedStmt).format(graphName=sql.Literal(graphName),cypherStmt=sql.Literal(cypher)))

    stmt = buildCypher(graphName, cypher, cols)
    cursor.execute(stmt)


# def execCypherWithReturn(conn:psycopg.connection, graphName:str, cypherStmt:str, columns:list=None , params:tuple=None) -> psycopg.cursor :
#     stmt = buildCypher(graphName, cypherStmt, columns)
#     return execSql(conn, stmt, False, params)

# def queryCypher(conn:psycopg.connection, graphName:str, cypherStmt:str, columns:list=None , params:tuple=None) -> psycopg.cursor :
#     return execCypherWithReturn(conn, graphName, cypherStmt, columns, params)


class Age:
    def __init__(self):
        self.connection = None    # psycopg connection]
        self.graphName = None

    # Connect to PostgreSQL Server and establish session and type extension environment.
    def connect(self, graph:str=None, dsn:str=None, connection_factory=None, cursor_factory=ClientCursor, **kwargs):
        conn = psycopg.connect(dsn, cursor_factory=cursor_factory, **kwargs)
        setUpAge(conn, graph)
        self.connection = conn
        self.graphName = graph
        return self

    def close(self):
        self.connection.close()

    def setGraph(self, graph:str):
        checkGraphCreated(self.connection, graph)
        self.graphName = graph
        return self

    def commit(self):
        self.connection.commit()

    def rollback(self):
        self.connection.rollback()

    def execCypher(self, cypherStmt:str, cols:list=None, params:tuple=None) -> psycopg.cursor :
        return execCypher(self.connection, self.graphName, cypherStmt, cols=cols, params=params)

    def cypher(self, cursor:psycopg.cursor, cypherStmt:str, cols:list=None, params:tuple=None) -> psycopg.cursor :
        return cypher(cursor, self.graphName, cypherStmt, cols=cols, params=params)

    # def execSql(self, stmt:str, commit:bool=False, params:tuple=None) -> psycopg.cursor :
    #     return execSql(self.connection, stmt, commit, params)


    # def execCypher(self, cypherStmt:str, commit:bool=False, params:tuple=None) -> psycopg.cursor :
    #     return execCypher(self.connection, self.graphName, cypherStmt, commit, params)

    # def execCypherWithReturn(self, cypherStmt:str, columns:list=None , params:tuple=None) -> psycopg.cursor :
    #     return execCypherWithReturn(self.connection, self.graphName, cypherStmt, columns, params)

    # def queryCypher(self, cypherStmt:str, columns:list=None , params:tuple=None) -> psycopg.cursor :
    #     return queryCypher(self.connection, self.graphName, cypherStmt, columns, params)
