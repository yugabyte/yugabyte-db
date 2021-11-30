import re 
import psycopg2 
from psycopg2 import errors
from psycopg2 import extensions as ext
from .exceptions import *
from .builder import ResultHandler , parseAgeValue, newResultHandler


_EXCEPTION_NoConnection = NoConnection()
_EXCEPTION_GraphNotSet = GraphNotSet()

WHITESPACE = re.compile('\s')

def setUpAge(conn:ext.connection, graphName:str):
    with conn.cursor() as cursor:
        cursor.execute("LOAD 'age';")
        cursor.execute("SET search_path = ag_catalog, '$user', public;")

        cursor.execute("SELECT typelem FROM pg_type WHERE typname='_agtype'")
        oid = cursor.fetchone()[0]
        if oid == None :
            raise AgeNotSet()

        AGETYPE = ext.new_type((oid,), 'AGETYPE', parseAgeValue)
        ext.register_type(AGETYPE)
        # ext.register_adapter(Path, marshalAgtValue)

        # Check graph exists
        if graphName != None:
            checkGraphCreated(conn, graphName)

# Create the graph, if it does not exist
def checkGraphCreated(conn:ext.connection, graphName:str):
    with conn.cursor() as cursor:
        cursor.execute("SELECT count(*) FROM ag_graph WHERE name=%s", (graphName,))
        if cursor.fetchone()[0] == 0:
            cursor.execute("SELECT create_graph(%s);", (graphName,))
            conn.commit()


def deleteGraph(conn:ext.connection, graphName:str):
    with conn.cursor() as cursor:
        cursor.execute("SELECT drop_graph(%s, true);", (graphName,))
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
    stmtArr.append("SELECT * from cypher('")
    stmtArr.append(graphName)
    stmtArr.append("', $$ ")
    stmtArr.append(cypherStmt)
    stmtArr.append(" $$) as (")
    stmtArr.append(','.join(columnExp))
    stmtArr.append(");")
    return "".join(stmtArr)

def execSql(conn:ext.connection, stmt:str, commit:bool=False, params:tuple=None) -> ext.cursor :
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
        raise SqlExcutionError("Excution ERR[" + str(cause) +"](" + stmt +")", cause)


def querySql(conn:ext.connection, stmt:str, params:tuple=None) -> ext.cursor :
    return execSql(conn, stmt, False, params)

# Execute cypher statement and return cursor.
# If cypher statement changes data (create, set, remove), 
# You must commit session(ag.commit()) 
# (Otherwise the execution cannot make any effect.)
def execCypher(conn:ext.connection, graphName:str, cypherStmt:str, cols:list=None, params:tuple=None) -> ext.cursor :
    if conn == None or conn.closed:
        raise _EXCEPTION_NoConnection

    stmt = buildCypher(graphName, cypherStmt, cols)
    
    cursor = conn.cursor()
    try:
        cursor.execute(stmt, params)
        return cursor
    except SyntaxError as cause:
        conn.rollback()
        raise cause
    except Exception as cause:
        conn.rollback()
        raise SqlExcutionError("Excution ERR[" + str(cause) +"](" + stmt +")", cause)


def cypher(cursor:ext.cursor, graphName:str, cypherStmt:str, cols:list=None, params:tuple=None) -> ext.cursor :
    stmt = buildCypher(graphName, cypherStmt, cols)
    cursor.execute(stmt, params)


# def execCypherWithReturn(conn:ext.connection, graphName:str, cypherStmt:str, columns:list=None , params:tuple=None) -> ext.cursor :
#     stmt = buildCypher(graphName, cypherStmt, columns)
#     return execSql(conn, stmt, False, params)

# def queryCypher(conn:ext.connection, graphName:str, cypherStmt:str, columns:list=None , params:tuple=None) -> ext.cursor :
#     return execCypherWithReturn(conn, graphName, cypherStmt, columns, params)


class Age:
    def __init__(self):
        self.connection = None    # psycopg2 connection]
        self.graphName = None

    # Connect to PostgreSQL Server and establish session and type extension environment.
    def connect(self, graph:str=None, dsn:str=None, connection_factory=None, cursor_factory=None, **kwargs):
        conn = psycopg2.connect(dsn, connection_factory, cursor_factory, **kwargs)
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
    
    def execCypher(self, cypherStmt:str, cols:list=None, params:tuple=None) -> ext.cursor :
        return execCypher(self.connection, self.graphName, cypherStmt, cols=cols, params=params)

    def cypher(self, cursor:ext.cursor, cypherStmt:str, cols:list=None, params:tuple=None) -> ext.cursor :
        return cypher(cursor, self.graphName, cypherStmt, cols=cols, params=params)

    # def execSql(self, stmt:str, commit:bool=False, params:tuple=None) -> ext.cursor :
    #     return execSql(self.connection, stmt, commit, params)
        
    
    # def execCypher(self, cypherStmt:str, commit:bool=False, params:tuple=None) -> ext.cursor :
    #     return execCypher(self.connection, self.graphName, cypherStmt, commit, params)

    # def execCypherWithReturn(self, cypherStmt:str, columns:list=None , params:tuple=None) -> ext.cursor :
    #     return execCypherWithReturn(self.connection, self.graphName, cypherStmt, columns, params)

    # def queryCypher(self, cypherStmt:str, columns:list=None , params:tuple=None) -> ext.cursor :
    #     return queryCypher(self.connection, self.graphName, cypherStmt, columns, params)



