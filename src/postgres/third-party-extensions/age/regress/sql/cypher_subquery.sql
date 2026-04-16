LOAD 'age';
SET search_path TO ag_catalog;

SELECT * FROM create_graph('subquery');

SELECT * FROM cypher('subquery', $$
								   CREATE (:person {name: "Briggite", age: 32})-[:knows]->(:person {name: "Takeshi", age: 28}),
										  (:person {name: "Chan", age: 45})<-[:knows]-(:person {name: "Faye", age: 25})-[:knows]->
										  (:person {name: "Tony", age: 34})-[:loved]->(:person {name : "Valerie", age: 33}),
										  (:person {name: "Calvin", age: 6})-[:knows]->(:pet {name: "Hobbes"}),
										  (:person {name: "Charlie", age: 8})-[:knows]->(:pet {name : "Snoopy"}),
										  (:pet {name: "Odie"})<-[:knows]-(:person {name: "Jon", age: 29})-[:knows]->(:pet {name: "Garfield"})
								 $$) AS (result agtype);

SELECT * FROM cypher('subquery', $$ MATCH (a) RETURN (a) $$) AS (result agtype);

SELECT * FROM cypher('subquery', $$ MATCH (a:person)
									WHERE EXISTS {(a:person)-[]->(:pet)}
									RETURN (a) $$) AS (result agtype);
--trying to use b when not defined, should create pattern
SELECT * FROM cypher('subquery', $$ MATCH (a:person)
										   WHERE EXISTS {(a:person)-[]->(b:pet)}
										   RETURN (a) $$) AS (result agtype);
--query inside
SELECT * FROM cypher('subquery', $$ MATCH (a:person)
									WHERE EXISTS {MATCH (a:person)-[]->(b:pet) RETURN b}
									RETURN (a) $$) AS (result agtype);

--repeat variable in match
SELECT * FROM cypher('subquery', $$ MATCH (a:person)
									WHERE EXISTS {
												  MATCH (a:person)
												  WHERE a.name = 'Takeshi'
												  RETURN a
												 }
									RETURN (a) $$) AS (result agtype);
--query inside, with WHERE
SELECT * FROM cypher('subquery', $$ MATCH (a:person)
									WHERE EXISTS {MATCH (a:person)-[]->(b:pet)
												  WHERE b.name = 'Briggite'
												  RETURN b}
									RETURN (a) $$) AS (result agtype);


--no return
SELECT * FROM cypher('subquery', $$ MATCH (a:person)
									WHERE EXISTS {MATCH (a:person)-[]->(b:pet)
												  WHERE a.name = 'Calvin'}
									RETURN (a) $$) AS (result agtype);

--union
SELECT * FROM cypher('subquery', $$ MATCH (a:person)
									WHERE EXISTS {
												  MATCH (a:person)-[]->(b:pet)
												  WHERE b.name = 'Hobbes'
												  RETURN b
												  UNION
												  MATCH (a:person)-[]->(c:person)
												  WHERE a.name = 'Faye'
												  RETURN c
												 }
									RETURN (a) $$) AS (result agtype);

-- union, mismatched var, should fail
SELECT * FROM cypher('subquery', $$ MATCH (a:person)
									WHERE EXISTS {
												  MATCH (a:person)-[]->(b:pet)
												  WHERE b.name = 'Snoopy'
												  RETURN c
												  UNION
												  MATCH (c:person)-[]->(d:person)
												  RETURN c
												 }
									RETURN (a) $$) AS (result agtype);

--union, no returns, not yet implemented, should error out
SELECT * FROM cypher('subquery', $$ MATCH (a:person)
									WHERE EXISTS {
												  MATCH (a:person)-[]->(b:pet)
												  WHERE a.name = 'Charlie'
												  UNION
												  MATCH (c:person)-[]->(d:person)
												 }
									RETURN (a) $$) AS (result agtype);

--union, only one has return, should fail
SELECT * FROM cypher('subquery', $$ MATCH (a:person)
									WHERE EXISTS {
												  MATCH (a:person)-[]->(b:pet)
												  WHERE a.name = 'Faye'
												  RETURN a
												  UNION
												  MATCH (c:person)-[]->(d:person)
												 }
									RETURN (a) $$) AS (result agtype);

--nesting (should return everything since a isn't sent all the way down)
SELECT * FROM cypher('subquery', $$ MATCH (a:person)
									WHERE EXISTS {
												  MATCH (b:person)
												  WHERE EXISTS {
																MATCH (c:person)
																WHERE c.name = 'Takeshi'
																RETURN c
															   }
												  RETURN b
												 }
									RETURN (a) $$) AS (result agtype);

--nesting same var multiple layers
SELECT * FROM cypher('subquery', $$ MATCH (a:person)
									WHERE EXISTS {
												  MATCH (a:person)
												  WHERE EXISTS {
																MATCH (a:person)
																WHERE a.name = 'Takeshi'
															   }
												 }
									RETURN (a) $$) AS (result agtype);

--nesting, accessing var in outer scope
SELECT * FROM cypher('subquery', $$ MATCH (a)
									WHERE EXISTS {
												  MATCH (b)
												  WHERE EXISTS {
																MATCH (c:person)
																WHERE b = c
															   }
												 }
									RETURN (a) $$) AS (result agtype);

--nesting, accessing indirection in outer scope
SELECT * FROM cypher('subquery', $$ MATCH (a:person)
										   WHERE EXISTS {
														 MATCH (b:person)
														 WHERE EXISTS {
																	   MATCH (c:person)
																	   WHERE b.name = 'Takeshi'
																	   RETURN c
																	  }
														}
										   RETURN (a) $$) AS (result agtype);

--nesting, accessing var 2+ levels up
SELECT * FROM cypher('subquery', $$ MATCH (a:person)
										   WHERE EXISTS {
														 MATCH (b:person)
														 WHERE EXISTS {
																	   MATCH (a:person)
																	   WHERE a.name = 'Takeshi'
																	   RETURN a
																	  }
														}
										   RETURN (a) $$) AS (result agtype);

--nesting, accessing indirection 2+ levels up
SELECT * FROM cypher('subquery', $$ MATCH (a:person)
									WHERE EXISTS {
												  MATCH (b:person)
												  WHERE EXISTS {
																MATCH (a:person)
																WHERE a = b
																RETURN a
															  }
												}
									RETURN (a) $$) AS (result agtype);

--EXISTS outside of WHERE
SELECT * FROM cypher('subquery', $$ MATCH (a:person)
									RETURN a, EXISTS {(a:person)-[]->(:pet)}
									$$) AS (a agtype, exists agtype);

--Var doesnt exist in outside scope, should fail
SELECT * FROM cypher('subquery', $$ RETURN 1,
										   EXISTS {
												   MATCH (b:person)-[]->(:pet)
												   RETURN a
												  }
									$$) AS (a agtype, exists agtype);

--- COUNT

--count pattern subquery in where
SELECT * FROM cypher('subquery', $$ MATCH (a:person)
									WHERE COUNT {(a:person)} > 1
									RETURN (a) $$) AS (result agtype);

--count pattern in return
SELECT * FROM cypher('subquery', $$ MATCH (a:person)
									RETURN COUNT{(a)-[]->(:pet)} $$) AS (result agtype);

--count pattern with WHERE
SELECT * FROM cypher('subquery', $$ MATCH (a:person)
									RETURN COUNT{(a)-[]->(b:pet)
												 WHERE b.name = 'Hobbes'}
									$$) AS (result agtype);

--solo match in where
SELECT * FROM cypher('subquery', $$ MATCH (a:person)
									WHERE COUNT{MATCH (a:person)-[]-()} > 1
									RETURN a $$) AS (result agtype);
--match where person has more than one pet
SELECT * FROM cypher('subquery', $$ MATCH (a:person)
									WHERE COUNT{MATCH (a:person)-[]-(:pet)} > 1
									RETURN a $$) AS (result agtype);

--match on labels
SELECT * FROM cypher('subquery', $$ MATCH (a:person)
									WHERE COUNT{MATCH (a:person)-[:knows]-()} > 1
									RETURN a $$) AS (result agtype);

SELECT * FROM cypher('subquery', $$ MATCH (a:person)
									WHERE COUNT{MATCH (a:person)-[:knows]-(:pet)} > 1
									RETURN a $$) AS (result agtype);

SELECT * FROM cypher('subquery', $$ MATCH (a:person)
									WHERE COUNT{MATCH (a:person)-[:knows]-(:person)} > 1
									RETURN a $$) AS (result agtype);

--solo match in return
SELECT * FROM cypher('subquery', $$ MATCH (a:person)
									RETURN COUNT{MATCH (a)} $$) AS (result agtype);

--match return in where
SELECT * FROM cypher('subquery', $$ MATCH (a:person)
									WHERE COUNT{MATCH (a) RETURN a} > 1 RETURN a $$) AS (result agtype);

--match return in return with return
SELECT * FROM cypher('subquery', $$ MATCH (a:person)
									RETURN COUNT{MATCH (a) RETURN a} $$) AS (result agtype);

--match where return
SELECT * FROM cypher('subquery', $$ MATCH (a:person)
									RETURN a.name, a.age, COUNT{MATCH (a)
														  WHERE a.age > 25
														  RETURN a.name} $$)
									AS (person agtype, age agtype, count agtype);

--counting number of relationships per node
SELECT * FROM cypher('subquery', $$ MATCH (a:person)
									RETURN a.name, COUNT{(a)-[]-()} $$) AS (name agtype, count agtype);

--nested counts
SELECT * FROM cypher('subquery', $$ MATCH (a:person)
									RETURN a.name, COUNT{MATCH (a) 
														 WHERE COUNT {MATCH (a)
														 			  WHERE a.age < 23
														 			  RETURN a } > 0 } $$)
									AS (name agtype, count agtype);

--incorrect variable reference
SELECT * FROM cypher('subquery', $$ MATCH (a:person)
									RETURN a.name, COUNT{MATCH (a) RETURN b} $$)
									AS (name agtype, count agtype);

--incorrect nested variable reference
SELECT * FROM cypher('subquery', $$ MATCH (a:person)
									RETURN a.name, COUNT{MATCH (a)
														 WHERE COUNT {MATCH (b) RETURN b } > 1
														 RETURN b} $$)
									AS (name agtype, count agtype);


--count nested with exists
SELECT * FROM cypher('subquery', $$ MATCH (a:person)
									RETURN a.name,
									COUNT{MATCH (a) WHERE EXISTS {MATCH (a)-[]-(:pet)
																  RETURN a }} $$)
									AS (name agtype, count agtype);

--
-- expression tree walker additional tests. want to test the nesting capabilties of the expr tree walker
--

--BoolExpr

-- with comparison
SELECT * FROM cypher('subquery', $$ MATCH (a:person)
									WHERE EXISTS {(a:person)-[]->(:pet)}
									AND a.name = 'Odie'
									RETURN (a) $$) AS (result agtype);

-- BoolExpr two subqueries
SELECT * FROM cypher('subquery', $$ MATCH (a:person)
									WHERE EXISTS {(a:person)-[]->(:pet)}
									OR EXISTS {(a:person)-[]->(:person)}
									RETURN (a) $$) AS (result agtype);

-- Nested BoolExpr
SELECT * FROM cypher('subquery', $$ MATCH (a:person)
									WHERE EXISTS {(a:person)-[]->(:pet)
									WHERE a.name = 'Charlie'}
									AND EXISTS {(a:person)-[]->(:person)}
									RETURN (a) $$) AS (result agtype);

-- CaseExpr

-- subqueries in WHEN statement in RETURN
SELECT * FROM cypher('subquery', $$ MATCH (a:person)-[]->(b)
									RETURN
									  a.name,
									  b.name,
									  CASE
									    WHEN EXISTS { MATCH (a)-[:loved]->(b) } THEN "There is LOVE!!!!!!"
									    WHEN EXISTS { MATCH (a)-[:knows]->(b) } THEN "There is a relationship"
									    ELSE "No relation"
									  END $$) AS (a agtype, b agtype, c agtype);

-- subqueries in THEN, WHERE
SELECT * FROM cypher('subquery', $$ MATCH (a:person)-[]->(b)
									WHERE
									  CASE a.name
									    WHEN "Jon" THEN EXISTS { MATCH (a)-[:knows]->(b) }
									    WHEN  "Tony" THEN EXISTS { MATCH (a)-[:loved]->(b) }
									    ELSE False
									  END = true RETURN a $$) AS (result agtype);

-- nested in another exists
SELECT * FROM cypher('subquery', $$ MATCH (a:person)-[]->(b)
									WHERE
									  EXISTS { MATCH (a) WHERE CASE a.name
									    WHEN "Jon" THEN EXISTS { MATCH (a)-[:knows]->(b) }
									    WHEN  "Tony" THEN EXISTS { MATCH (a)-[:loved]->(b) }
									    ELSE False}
									  END = true RETURN a $$) AS (result agtype);

-- CoalesceExpr

--coalesce, nested in where
SELECT * FROM cypher('subquery', $$ MATCH (a:person)-[]->(b)
									WHERE COALESCE( EXISTS {MATCH (a)-[:knows]->(b)
														    WHERE COALESCE( EXISTS {MATCH (a)
																				    WHERE a.name = "Calvin"
																				    OR a.name = "Charlie"})})
									RETURN a $$) AS (result agtype);

--coalesce, nested in return
SELECT * FROM cypher('subquery', $$ MATCH (a:person)-[]->(b)
									RETURN COALESCE( EXISTS {MATCH (a)-[:knows]->(b)
														    WHERE COALESCE( EXISTS {MATCH (a)
																				    WHERE a.name = "Calvin"
																				    OR a.name = "Charlie"})}) AS coalescefunc
									$$) AS (name agtype, result agtype);

-- maps

--nested exists maps
SELECT * FROM cypher('subquery', $$MATCH (a)
								   WHERE {cond : true} = { cond : EXISTS {(a)
																  WHERE {age: a.age } = {age: a.age > 22}}}
								   RETURN a $$ ) AS (a agtype);

--nested exists in return
SELECT * FROM cypher('subquery', $$MATCH (a)
								   RETURN a, {cond : true} = { cond : EXISTS {(a)
																      WHERE {age: a.age } = {age: a.age > 22}}}
								   $$ ) AS (a agtype, cond agtype);


-- map projection

--where
SELECT * FROM cypher('subquery', $$MATCH (a)
								   WITH { a : EXISTS {(a) WHERE a.name = 'Chan'}} as matchmap, a
								   WHERE true = matchmap.a
								   RETURN a $$ ) AS (a agtype);

--return
SELECT * FROM cypher('subquery', $$MATCH (a)
								   RETURN a.name,
								   { a : EXISTS {(a) WHERE a.name = 'Chan'}} $$ ) AS (a agtype, cond agtype);

--lists

--list
SELECT * FROM cypher('subquery', $$ MATCH (a:pet) WHERE true IN [EXISTS {
MATCH (a) WHERE a.name = 'Hobbes' RETURN a}] RETURN a $$) AS (result agtype);

--nested in list
SELECT * FROM cypher('subquery', $$ MATCH (a:pet) WHERE [true] IN [[EXISTS {
MATCH (a) WHERE a.name = 'Hobbes' RETURN a}]] RETURN a $$) AS (result agtype);

--exist nested in list nested in list
SELECT * FROM cypher('subquery', $$ MATCH (a:pet) WHERE [true] IN [[EXISTS {
MATCH (a) WHERE EXISTS {MATCH (a)-[]-()} RETURN a}]] RETURN a $$) AS (result agtype);

--
-- Cleanup
--
SELECT * FROM drop_graph('subquery', true);

--
-- End of tests
--
