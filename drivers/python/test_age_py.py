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

from age.models import Vertex
import unittest
import decimal
import age 

DSN = "host=127.0.0.1 port=5432 dbname=postgres user=postgres password=agens"
TEST_HOST = "127.0.0.1"
TEST_PORT = 5432
TEST_DB = "postgres"
TEST_USER = "postgres"
TEST_PASSWORD = "agens"
TEST_GRAPH_NAME = "test_graph"

class TestAgeBasic(unittest.TestCase):
    ag = None
    def setUp(self):
        print("Connecting to Test Graph.....")
        self.ag = age.connect(graph=TEST_GRAPH_NAME, host=TEST_HOST, port=TEST_PORT, dbname=TEST_DB, user=TEST_USER, password=TEST_PASSWORD)


    def tearDown(self):
        # Clear test data
        print("Deleting Test Graph.....")
        age.deleteGraph(self.ag.connection, self.ag.graphName)
        self.ag.close()

    def testExec(self):
        ag = self.ag
        # Create and Return single column
        cursor = ag.execCypher("CREATE (n:Person {name: %s, title: 'Developer'}) RETURN n", params=('Andy',))
        for row in cursor:
            print(Vertex, type(row[0]))

        
        # Create and Return multi columns
        cursor = ag.execCypher("CREATE (n:Person {name: %s, title: %s}) RETURN id(n), n.name", cols=['id','name'], params=('Jack','Manager'))
        row = cursor.fetchone()
        print(row[0], row[1])
        self.assertEqual(int, type(row[0]))
        ag.commit()

            
        
    def testQuery(self):
        ag = self.ag
        ag.execCypher("CREATE (n:Person {name: %s}) ", params=('Jack',))
        ag.execCypher("CREATE (n:Person {name: %s}) ", params=('Andy',))
        ag.execCypher("CREATE (n:Person {name: %s}) ", params=('Smith',))
        ag.execCypher("MATCH (a:Person), (b:Person) WHERE a.name = 'Andy' AND b.name = 'Jack' CREATE (a)-[r:workWith {weight: 3}]->(b)")
        ag.execCypher("""MATCH (a:Person), (b:Person) 
                    WHERE  a.name = %s AND b.name = %s 
                    CREATE p=((a)-[r:workWith]->(b)) """, params=('Jack', 'Smith',))
        
        ag.commit()

        cursor = ag.execCypher("MATCH p=()-[:workWith]-() RETURN p")
        for row in cursor:
            path = row[0]
            print("START:", path[0])
            print("EDGE:", path[1])
            print("END:", path[2])  

        cursor = ag.execCypher("MATCH p=(a)-[b]-(c) WHERE b.weight>2 RETURN a,label(b), b.weight, c", cols=["a","bl","bw", "c"], params=(2,))
        for row in cursor:
            start = row[0]
            edgel = row[1]
            edgew = row[2]
            end = row[3]
            print(start["name"] , edgel, edgew, end["name"]) 
            
        
    def testChangeData(self):
        ag = self.ag
        # Create Vertices
        # Commit automatically
        ag.execCypher("CREATE (n:Person {name: 'Joe'})")

        cursor = ag.execCypher("CREATE (n:Person {name: %s, title: 'Developer'}) RETURN n", params=('Smith',))
        row = cursor.fetchone()
        print("CREATED: ", row[0])
        
        # You must commit explicitly
        ag.commit()

        cursor = ag.execCypher("MATCH (n:Person {name: %s}) SET n.title=%s RETURN n", params=('Smith','Manager',))
        row = cursor.fetchone()
        vertex = row[0] 
        title1 = vertex["title"]
        print("SET title: ", title1)

        ag.commit()
    
        cursor = ag.execCypher("MATCH (p:Person {name: 'Smith'}) RETURN p.title")
        row = cursor.fetchone()
        title2 = row[0]

        self.assertEqual(title1, title2)

        cursor = ag.execCypher("MATCH (n:Person {name: %s}) SET n.bigNum=-6.45161e+46::numeric RETURN n", params=('Smith',))
        row = cursor.fetchone()
        vertex = row[0]
        for row in cursor:
            print("SET bigNum: ", vertex)
        
        bigNum1 = vertex["bigNum"]

        self.assertEqual(decimal.Decimal("-6.45161e+46"), bigNum1)
        ag.commit()


        cursor = ag.execCypher("MATCH (p:Person {name: 'Smith'}) RETURN p.bigNum")
        row = cursor.fetchone()
        bigNum2 = row[0]

        self.assertEqual(bigNum1, bigNum2)


        cursor = ag.execCypher("MATCH (n:Person {name: %s}) REMOVE n.title RETURN n", params=('Smith',))
        for row in cursor:
            print("REMOVE Prop title: ", row[0])

        # You must commit explicitly
        ag.commit()

    
    def testCypher(self):
        ag = self.ag

        with ag.connection.cursor() as cursor:
            try :
                ag.cypher(cursor, "CREATE (n:Person {name: %s}) ", params=('Jone',))
                ag.cypher(cursor, "CREATE (n:Person {name: %s}) ", params=('Jack',))
                ag.cypher(cursor, "CREATE (n:Person {name: %s}) ", params=('Andy',))
                ag.cypher(cursor, "CREATE (n:Person {name: %s}) ", params=('Smith',))
                ag.cypher(cursor, "CREATE (n:Person {name: %s}) ", params=('Tom',))

                # You must commit explicitly
                ag.commit()
            except Exception as ex:
                print(ex)
                ag.rollback()

        with ag.connection.cursor() as cursor:
            try :# Create Edges
                ag.cypher(cursor,"MATCH (a:Person), (b:Person) WHERE a.name = 'Joe' AND b.name = 'Smith' CREATE (a)-[r:workWith {weight: 3}]->(b)")
                ag.cypher(cursor,"MATCH (a:Person), (b:Person) WHERE  a.name = 'Andy' AND b.name = 'Tom' CREATE (a)-[r:workWith {weight: 1}]->(b)")
                ag.cypher(cursor,"MATCH (a:Person {name: 'Jack'}), (b:Person {name: 'Andy'}) CREATE (a)-[r:workWith {weight: 5}]->(b)")

                # You must commit explicitly
                ag.commit()
            except Exception as ex:
                print(ex)
                ag.rollback()
        

        # With Params
        cursor = ag.execCypher("""MATCH (a:Person), (b:Person) 
                WHERE  a.name = %s AND b.name = %s 
                CREATE p=((a)-[r:workWith]->(b)) RETURN p""", 
                params=('Andy', 'Smith',))

        for row in cursor:
            print(row[0])
            
        cursor = ag.execCypher("""MATCH (a:Person {name: 'Joe'}), (b:Person {name: 'Jack'}) 
                CREATE p=((a)-[r:workWith {weight: 5}]->(b))
                RETURN p """)

        for row in cursor:
            print(row[0])
            


    def testMultipleEdges(self):
        ag = self.ag
        with ag.connection.cursor() as cursor:
            try :
                ag.cypher(cursor, "CREATE (n:Country {name: %s}) ", params=('USA',))
                ag.cypher(cursor, "CREATE (n:Country {name: %s}) ", params=('France',))
                ag.cypher(cursor, "CREATE (n:Country {name: %s}) ", params=('Korea',))
                ag.cypher(cursor, "CREATE (n:Country {name: %s}) ", params=('Russia',))

                # You must commit explicitly after all executions.
                ag.connection.commit()
            except Exception as ex:
                ag.rollback()
                raise ex

        with ag.connection.cursor() as cursor:
            try :# Create Edges
                ag.cypher(cursor,"MATCH (a:Country), (b:Country) WHERE a.name = 'USA' AND b.name = 'France' CREATE (a)-[r:distance {unit:'miles', value: 4760}]->(b)")
                ag.cypher(cursor,"MATCH (a:Country), (b:Country) WHERE  a.name = 'France' AND b.name = 'Korea' CREATE (a)-[r:distance {unit: 'km', value: 9228}]->(b)")
                ag.cypher(cursor,"MATCH (a:Country {name: 'Korea'}), (b:Country {name: 'Russia'}) CREATE (a)-[r:distance {unit:'km', value: 3078}]->(b)")

                # You must commit explicitly
                ag.connection.commit()
            except Exception as ex:
                ag.rollback()
                raise ex


        cursor = ag.execCypher("""MATCH p=(:Country {name:"USA"})-[:distance]-(:Country)-[:distance]-(:Country) 
                RETURN p""")

        count = 0
        for row in cursor:
            path = row[0]
            indent = ""
            for e in path:
                if e.gtype == age.TP_VERTEX:
                    print(indent, e.label, e["name"])
                elif e.gtype == age.TP_EDGE:
                    print(indent, e.label, e["value"], e["unit"])
                else:
                    print(indent, "Unknown element.", e)
                
                count += 1
                indent += " >"

        self.assertEqual(5,count)

    def testCollect(self):
        ag = self.ag
        
        with ag.connection.cursor() as cursor:
            try :
                ag.cypher(cursor, "CREATE (n:Person {name: %s}) ", params=('Joe',))
                ag.cypher(cursor, "CREATE (n:Person {name: %s}) ", params=('Jack',))
                ag.cypher(cursor, "CREATE (n:Person {name: %s}) ", params=('Andy',))
                ag.cypher(cursor, "CREATE (n:Person {name: %s}) ", params=('Smith',))
                ag.cypher(cursor, "CREATE (n:Person {name: %s}) ", params=('Tom',))

                # You must commit explicitly
                ag.commit()
            except Exception as ex:
                print(ex)
                ag.rollback()

        with ag.connection.cursor() as cursor:
            try :# Create Edges
                ag.cypher(cursor,"MATCH (a:Person), (b:Person) WHERE a.name = 'Joe' AND b.name = 'Smith' CREATE (a)-[r:workWith {weight: 3}]->(b)")
                ag.cypher(cursor,"MATCH (a:Person), (b:Person) WHERE  a.name = 'Joe' AND b.name = 'Tom' CREATE (a)-[r:workWith {weight: 1}]->(b)")
                ag.cypher(cursor,"MATCH (a:Person {name: 'Joe'}), (b:Person {name: 'Andy'}) CREATE (a)-[r:workWith {weight: 5}]->(b)")

                # You must commit explicitly
                ag.commit()
            except Exception as ex:
                print(ex)
                ag.rollback()

        print(" - COLLECT 1 --------")
        with ag.connection.cursor() as cursor:
            ag.cypher(cursor, "MATCH (a)-[:workWith]->(c) WITH a as V, COLLECT(c) as CV RETURN V.name, CV", cols=["V","CV"])
            for row in cursor:
                nm = row[0]
                collected = row[1]
                print(nm, "workWith", [i["name"] for i in collected])
                self.assertEqual(3,len(collected))

   
        print(" - COLLECT 2 --------")
        for row in ag.execCypher("MATCH (a)-[:workWith]->(c) WITH a as V, COLLECT(c) as CV RETURN V.name, CV", cols=["V1","CV"]):
            nm = row[0]
            collected = row[1]
            print(nm, "workWith", [i["name"] for i in collected])
            self.assertEqual(3,len(collected))

if __name__ == '__main__':
    unittest.main()