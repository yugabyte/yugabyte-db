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

from age.models import Vertex
import unittest
import decimal
import age
import argparse

TEST_HOST = "localhost"
TEST_PORT = 5432
TEST_DB = "postgres"
TEST_USER = "postgres"
TEST_PASSWORD = "agens"
TEST_GRAPH_NAME = "test_graph"


class TestAgeBasic(unittest.TestCase):
    ag = None
    args: argparse.Namespace = argparse.Namespace(
        host=TEST_HOST,
        port=TEST_PORT,
        database=TEST_DB,
        user=TEST_USER,
        password=TEST_PASSWORD,
        graphName=TEST_GRAPH_NAME
    )

    def setUp(self):
        print("Connecting to Test Graph.....")
        args = dict(
            host=self.args.host,
            port=self.args.port,
            dbname=self.args.database,
            user=self.args.user,
            password=self.args.password,
        )

        dsn = "host={host} port={port} dbname={dbname} user={user} password={password}".format(
            **args
        )
        self.ag = age.connect(dsn, graph=self.args.graphName, **args)

    def tearDown(self):
        # Clear test data
        print("Deleting Test Graph.....")
        age.deleteGraph(self.ag.connection, self.ag.graphName)
        self.ag.close()

    def testExec(self):
        print("\n---------------------------------------------------")
        print("Test 1: Checking single and multi column Returns.....")
        print("---------------------------------------------------\n")

        ag = self.ag
        # Create and Return single column
        cursor = ag.execCypher(
            "CREATE (n:Person {name: %s, title: 'Developer'}) RETURN n",
            params=("Andy",),
        )
        for row in cursor:
            print("Vertex: %s , Type: %s " % (Vertex, type(row[0])))

        # Create and Return multi columns
        cursor = ag.execCypher(
            "CREATE (n:Person {name: %s, title: %s}) RETURN id(n), n.name",
            cols=["id", "name"],
            params=("Jack", "Manager"),
        )
        row = cursor.fetchone()
        print("Id: %s , Name: %s" % (row[0], row[1]))
        self.assertEqual(int, type(row[0]))
        ag.commit()
        print("\nTest 1 Successful....")

    def testQuery(self):
        print("\n--------------------------------------------------")
        print("Test 2: Testing CREATE and query relationships.....")
        print("--------------------------------------------------\n")

        ag = self.ag
        ag.execCypher("CREATE (n:Person {name: %s}) ", params=("Jack",))
        ag.execCypher("CREATE (n:Person {name: %s}) ", params=("Andy",))
        ag.execCypher("CREATE (n:Person {name: %s}) ", params=("Smith",))
        ag.execCypher(
            "MATCH (a:Person), (b:Person) WHERE a.name = 'Andy' AND b.name = 'Jack' CREATE (a)-[r:worksWith {weight: 3}]->(b)"
        )
        ag.execCypher(
            """MATCH (a:Person), (b:Person)
                    WHERE  a.name = %s AND b.name = %s
                    CREATE p=((a)-[r:worksWith]->(b)) """,
            params=(
                "Jack",
                "Smith",
            ),
        )

        ag.commit()

        cursor = ag.execCypher("MATCH p=()-[:worksWith]-() RETURN p")
        for row in cursor:
            path = row[0]
            print("START:", path[0])
            print("EDGE:", path[1])
            print("END:", path[2])

        cursor = ag.execCypher(
            "MATCH p=(a)-[b]-(c) WHERE b.weight>%s RETURN a,label(b), b.weight, c",
            cols=["a", "bl", "bw", "c"],
            params=(2,),
        )
        for row in cursor:
            start = row[0]
            edgel = row[1]
            edgew = row[2]
            end = row[3]
            print(
                "Relationship: %s %s %s. Edge weight: %s"
                % (start["name"], edgel, end["name"], edgew)
            )
            # Assert that the weight of the edge is greater than 2
            self.assertEqual(edgew > 2, True)
        print("\nTest 2 Successful...")

    def testChangeData(self):
        print("\n-------------------------------------------------------")
        print("Test 3: Testing changes in data using SET and REMOVE.....")
        print("-------------------------------------------------------\n")

        ag = self.ag
        # Create Vertices
        # Commit automatically
        ag.execCypher("CREATE (n:Person {name: 'Joe'})")

        cursor = ag.execCypher(
            "CREATE (n:Person {name: %s, title: 'Developer'}) RETURN n",
            params=("Smith",),
        )
        row = cursor.fetchone()
        print("CREATED: ", row[0])

        # You must commit explicitly
        ag.commit()

        cursor = ag.execCypher(
            "MATCH (n:Person {name: %s}) SET n.title=%s RETURN n",
            params=(
                "Smith",
                "Manager",
            ),
        )
        row = cursor.fetchone()
        vertex = row[0]
        title1 = vertex["title"]
        print("SET title: ", title1)

        ag.commit()

        cursor = ag.execCypher("MATCH (p:Person {name: 'Smith'}) RETURN p.title")
        row = cursor.fetchone()
        title2 = row[0]

        self.assertEqual(title1, title2)

        cursor = ag.execCypher(
            "MATCH (n:Person {name: %s}) SET n.bigNum=-6.45161e+46::numeric RETURN n",
            params=("Smith",),
        )
        row = cursor.fetchone()
        vertex = row[0]
        for row in cursor:
            print("SET bigNum: ", vertex["bigNum"])

        bigNum1 = vertex["bigNum"]

        self.assertEqual(decimal.Decimal("-6.45161e+46"), bigNum1)
        ag.commit()

        cursor = ag.execCypher("MATCH (p:Person {name: 'Smith'}) RETURN p.bigNum")
        row = cursor.fetchone()
        bigNum2 = row[0]

        self.assertEqual(bigNum1, bigNum2)

        cursor = ag.execCypher(
            "MATCH (n:Person {name: %s}) REMOVE n.title RETURN n", params=("Smith",)
        )
        for row in cursor:
            print("REMOVE Prop title: ", row[0])
            # Assert that the title property is removed
            self.assertIsNone(row[0].properties.get("title"))
        print("\nTest 3 Successful....")

        # You must commit explicitly
        ag.commit()

    def testCypher(self):
        print("\n--------------------------")
        print("Test 4: Testing Cypher.....")
        print("--------------------------\n")

        ag = self.ag

        with ag.connection.cursor() as cursor:
            try:
                ag.cypher(cursor, "CREATE (n:Person {name: %s}) ", params=("Joe",))
                ag.cypher(cursor, "CREATE (n:Person {name: %s}) ", params=("Jack",))
                ag.cypher(cursor, "CREATE (n:Person {name: %s}) ", params=("Andy",))
                ag.cypher(cursor, "CREATE (n:Person {name: %s}) ", params=("Smith",))
                ag.cypher(cursor, "CREATE (n:Person {name: %s}) ", params=("Tom",))

                # You must commit explicitly
                ag.commit()
            except Exception as ex:
                print(ex)
                ag.rollback()

        with ag.connection.cursor() as cursor:
            try:  # Create Edges
                ag.cypher(
                    cursor,
                    "MATCH (a:Person), (b:Person) WHERE a.name = 'Joe' AND b.name = 'Smith' CREATE (a)-[r:worksWith {weight: 3}]->(b)",
                )
                ag.cypher(
                    cursor,
                    "MATCH (a:Person), (b:Person) WHERE  a.name = 'Andy' AND b.name = 'Tom' CREATE (a)-[r:worksWith {weight: 1}]->(b)",
                )
                ag.cypher(
                    cursor,
                    "MATCH (a:Person {name: 'Jack'}), (b:Person {name: 'Andy'}) CREATE (a)-[r:worksWith {weight: 5}]->(b)",
                )

                # You must commit explicitly
                ag.commit()
            except Exception as ex:
                print(ex)
                ag.rollback()

        # With Params
        cursor = ag.execCypher(
            """MATCH (a:Person), (b:Person)
                WHERE  a.name = %s AND b.name = %s
                CREATE p=((a)-[r:worksWith]->(b)) RETURN p""",
            params=(
                "Andy",
                "Smith",
            ),
        )

        for row in cursor:
            print("CREATED EDGE: %s" % row[0])

        cursor = ag.execCypher("""MATCH (a:Person {name: 'Joe'}), (b:Person {name: 'Jack'})
                CREATE p=((a)-[r:worksWith {weight: 5}]->(b))
                RETURN p """)

        for row in cursor:
            print("CREATED EDGE WITH PROPERTIES: %s" % row[0])
            self.assertEqual(row[0][1].properties["weight"], 5)

        print("\nTest 4 Successful...")

    def testMultipleEdges(self):
        print("\n------------------------------------")
        print("Test 5: Testing Multiple Edges.....")
        print("------------------------------------\n")

        ag = self.ag
        with ag.connection.cursor() as cursor:
            try:
                ag.cypher(cursor, "CREATE (n:Country {name: %s}) ", params=("USA",))
                ag.cypher(cursor, "CREATE (n:Country {name: %s}) ", params=("France",))
                ag.cypher(cursor, "CREATE (n:Country {name: %s}) ", params=("Korea",))
                ag.cypher(cursor, "CREATE (n:Country {name: %s}) ", params=("Russia",))

                # You must commit explicitly after all executions.
                ag.connection.commit()
            except Exception as ex:
                ag.rollback()
                raise ex

        with ag.connection.cursor() as cursor:
            try:  # Create Edges
                ag.cypher(
                    cursor,
                    "MATCH (a:Country), (b:Country) WHERE a.name = 'USA' AND b.name = 'France' CREATE (a)-[r:distance {unit:'miles', value: 4760}]->(b)",
                )
                ag.cypher(
                    cursor,
                    "MATCH (a:Country), (b:Country) WHERE  a.name = 'France' AND b.name = 'Korea' CREATE (a)-[r:distance {unit: 'km', value: 9228}]->(b)",
                )
                ag.cypher(
                    cursor,
                    "MATCH (a:Country {name: 'Korea'}), (b:Country {name: 'Russia'}) CREATE (a)-[r:distance {unit:'km', value: 3078}]->(b)",
                )

                # You must commit explicitly
                ag.connection.commit()
            except Exception as ex:
                ag.rollback()
                raise ex

        cursor = ag.execCypher("""MATCH p=(:Country {name:"USA"})-[:distance]-(:Country)-[:distance]-(:Country)
                RETURN p""")

        count = 0
        output = []
        for row in cursor:
            path = row[0]
            for e in path:
                if e.gtype == age.TP_VERTEX:
                    output.append(e.label + " " + e["name"])
                elif e.gtype == age.TP_EDGE:
                    output.append(
                        "---- (distance " + str(e["value"]) + " " + e["unit"] + ") --->"
                    )
                else:
                    output.append("Unknown element. " + str(e))

                count += 1

        formatted_output = " ".join(output)
        print("PATH WITH MULTIPLE EDGES: %s" % formatted_output)
        self.assertEqual(5, count)

        print("\nTest 5 Successful...")

    def testCollect(self):
        print("\n--------------------------")
        print("Test 6: Testing COLLECT.....")
        print("--------------------------\n")

        ag = self.ag

        with ag.connection.cursor() as cursor:
            try:
                ag.cypher(cursor, "CREATE (n:Person {name: %s}) ", params=("Joe",))
                ag.cypher(cursor, "CREATE (n:Person {name: %s}) ", params=("Jack",))
                ag.cypher(cursor, "CREATE (n:Person {name: %s}) ", params=("Andy",))
                ag.cypher(cursor, "CREATE (n:Person {name: %s}) ", params=("Smith",))
                ag.cypher(cursor, "CREATE (n:Person {name: %s}) ", params=("Tom",))

                # You must commit explicitly
                ag.commit()
            except Exception as ex:
                print(ex)
                ag.rollback()

        with ag.connection.cursor() as cursor:
            try:  # Create Edges
                ag.cypher(
                    cursor,
                    "MATCH (a:Person), (b:Person) WHERE a.name = 'Joe' AND b.name = 'Smith' CREATE (a)-[r:worksWith {weight: 3}]->(b)",
                )
                ag.cypher(
                    cursor,
                    "MATCH (a:Person), (b:Person) WHERE  a.name = 'Joe' AND b.name = 'Tom' CREATE (a)-[r:worksWith {weight: 1}]->(b)",
                )
                ag.cypher(
                    cursor,
                    "MATCH (a:Person {name: 'Joe'}), (b:Person {name: 'Andy'}) CREATE (a)-[r:worksWith {weight: 5}]->(b)",
                )

                # You must commit explicitly
                ag.commit()
            except Exception as ex:
                print(ex)
                ag.rollback()

        print(" -------- TESTING COLLECT #1 --------")
        with ag.connection.cursor() as cursor:
            ag.cypher(
                cursor,
                "MATCH (a)-[:worksWith]->(c) WITH a as V, COLLECT(c) as CV RETURN V.name, CV",
                cols=["V", "CV"],
            )
            for row in cursor:
                nm = row[0]
                collected = row[1]
                print(nm, "worksWith", [i["name"] for i in collected])
                self.assertEqual(3, len(collected))

        print(" -------- TESTING COLLECT #2 --------")
        for row in ag.execCypher(
            "MATCH (a)-[:worksWith]->(c) WITH a as V, COLLECT(c) as CV RETURN V.name, CV",
            cols=["V1", "CV"],
        ):
            nm = row[0]
            collected = row[1]
            print(nm, "worksWith", [i["name"] for i in collected])
            self.assertEqual(3, len(collected))
        print("\nTest 6 Successful...")

    def testSerialization(self):
        print("\n---------------------------------------")
        print("Test 6: Testing Vertex Serialization.....")
        print("-----------------------------------------\n")

        ag = self.ag

        with ag.connection.cursor() as cursor:
            try:
                ag.cypher(cursor, "CREATE (n:Person {name: %s}) ", params=("Joe",))
                ag.cypher(cursor, "CREATE (n:Person {name: %s}) ", params=("Jack",))
                ag.cypher(cursor, "CREATE (n:Person {name: %s}) ", params=("Andy",))
                ag.cypher(cursor, "CREATE (n:Person {name: %s}) ", params=("Smith",))
                ag.cypher(cursor, "CREATE (n:Person {name: %s}) ", params=("Tom",))

                # You must commit explicitly
                ag.commit()
            except Exception as ex:
                print(ex)
                ag.rollback()

        print(" -------- TESTING Output #1 --------")
        cursor = ag.execCypher("MATCH (n) RETURN n")

        for row in cursor:
            vertex = row[0]
            try:
                # json.loads will fail if the json str is not properly formatted
                as_dict = json.loads(vertex.toJson())
                print("Vertex.toJson() returns a correct json string.")
                assert True
            except:
                assert False

        print(" -------- TESTING Output #2 --------")
        cursor = ag.execCypher("MATCH (n) RETURN n")

        for row in cursor:
            vertex = row[0]
            as_str = vertex.toString()
            # Checking if the trailing comma appears in .toString() output
            self.assertFalse(as_str.endswith(", }}::VERTEX"))
        print("Vertex.toString() 'properties' field is formatted properly.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()

    parser.add_argument(
        "-host",
        "--host",
        help='Optional Host Name. Default Host is "127.0.0.1" ',
        default=TEST_HOST,
    )
    parser.add_argument(
        "-port",
        "--port",
        help="Optional Port Number. Default port no is 5432",
        default=TEST_PORT,
    )
    parser.add_argument(
        "-db", "--database", help="Required Database Name", default=TEST_DB
    )
    parser.add_argument(
        "-u", "--user", help="Required Username Name", default=TEST_USER
    )
    parser.add_argument(
        "-pass",
        "--password",
        help="Required Password for authentication",
        default=TEST_PASSWORD,
    )
    parser.add_argument(
        "-gn",
        "--graphName",
        help='Optional Graph Name to be created. Default graphName is "test_graph"',
        default=TEST_GRAPH_NAME,
    )

    args = parser.parse_args()
    suite = unittest.TestSuite()
    suite.addTest(TestAgeBasic("testExec"))
    suite.addTest(TestAgeBasic("testQuery"))
    suite.addTest(TestAgeBasic("testChangeData"))
    suite.addTest(TestAgeBasic("testCypher"))
    suite.addTest(TestAgeBasic("testMultipleEdges"))
    suite.addTest(TestAgeBasic("testCollect"))
    suite.addTest(TestAgeBasic("testSerialization"))
    TestAgeBasic.args = args
    unittest.TextTestRunner().run(suite)
