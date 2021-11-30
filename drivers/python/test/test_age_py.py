import unittest
import age 

DSN = "host=172.17.0.3 port=5432 dbname=postgres user=postgres password=agens"
TEST_GRAPH_NAME = "testGraph"
class TestAgeBasic(unittest.TestCase):
    ag = None

    def __init__(self, methodName: str) -> None:
        super().__init__(methodName=methodName)
        self.ag = age.connect(dsn=DSN, graph=TEST_GRAPH_NAME)

    def __del__(self):
        # Clear test data
        self.ag.execCypher("MATCH (n:Person) DETACH DELETE n RETURN *")
        self.ag.execSql("DROP TABLE IF EXISTS infos")
        self.ag.commit()
        self.ag.deleteGraph(self.ag.graphName)
        self.ag.close()
    
    def tearDown(self) -> None:
        self.ag.execCypher("MATCH (n:Person) DETACH DELETE n RETURN *")
        self.ag.commit()
        return super().tearDown()

    def test_vertices(self):
        ag = self.ag
        # Create Vertices
        ag.execCypher("CREATE (n:Person {name: 'Joe'})")
        ag.execCypher("CREATE (n:Person {name: 'Smith'})")
        ag.execCypher("CREATE (n:Person {name: %s})", ('Jack',))
        ag.execCypher("CREATE (n:Person {name: 'Andy', title: 'Developer'})")
        ag.execCypher("CREATE (n:Person {name: %s, title: %s})", ('Tom','Developer',))
        ag.commit()

        # Query Vertices with result parsed full graph.
        ag.queryCypher("MATCH (n:Person) RETURN n")
        graph = ag.graph()
        self.assertEqual(5, len(graph), "Create and retrieve vertices.")

        for vertex in graph:
            self.assertEqual( age.models.Vertex, type(vertex), "Retrieve and unmarshal Vertex.")

        # Query Vertices with parsed row cursor.
        ag.queryCypher("MATCH (n:Person) RETURN n")
        for vertex in ag.rows():
            self.assertIsNotNone(vertex.id)
            self.assertIsNotNone(vertex.properties["name"])
            self.assertIsNotNone(vertex.toJson())

    def test_paths(self):
        ag = self.ag
        # Create Vertices
        ag.execCypher("CREATE (n:Person {name: 'Joe'})")
        ag.execCypher("CREATE (n:Person {name: 'Smith'})")
        ag.execCypher("CREATE (n:Person {name: %s})", ('Jack',))
        ag.execCypher("CREATE (n:Person {name: 'Andy', title: 'Developer'})")
        ag.execCypher("CREATE (n:Person {name: %s, title: %s})", ('Tom','Developer',))
        ag.commit()
        # Create Edges
        ag.execCypher("MATCH (a:Person), (b:Person) WHERE a.name = 'Joe' AND b.name = 'Smith' CREATE (a)-[r:workWith {weight: 3}]->(b)")
        ag.execCypher("MATCH (a:Person), (b:Person) WHERE  a.name = 'Andy' AND b.name = 'Tom' CREATE (a)-[r:workWith {weight: 1}]->(b)")
        ag.execCypher("MATCH (a:Person {name: 'Jack'}), (b:Person {name: 'Andy'}) CREATE (a)-[r:workWith {weight: 5}]->(b)")
        ag.execCypher("MATCH (a:Person {name: 'Joe'}), (b:Person {name: 'Jack'}) CREATE (a)-[r:workWith {weight: 5}]->(b)")
        ag.commit()

        ag.queryCypher("MATCH p=()-[:workWith]-() RETURN p")
        graph = ag.graph()
        self.assertEqual(8, len(graph), "Create and retrieve paths.")
        for path in graph:
            self.assertEqual( age.models.Path, type(path), "Retrieve and unmarshal Path.")
            

if __name__ == '__main__':
    unittest.main()