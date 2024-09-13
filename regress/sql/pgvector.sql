/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

LOAD 'age';
SET search_path=ag_catalog;

SELECT create_graph('graph');

-- Should error out
SELECT * FROM cypher('graph', $$ RETURN cosine_distance("[1,2,3]", "[1,2,3]") $$) AS (n agtype);

-- Create the extension in the public schema
CREATE EXTENSION vector SCHEMA public;

-- Should error out
SELECT * FROM cypher('graph', $$ RETURN cosine_distance("[1,2,3]", "[1,2,3]") $$) AS (n agtype);

-- Should work
SET search_path=ag_catalog, public;

SELECT create_graph('graph');
SELECT * FROM cypher('graph', $$ RETURN "[1.22,2.22,3.33]"::vector $$) AS (n vector);
SELECT * FROM cypher('graph', $$ RETURN "[1.22,2.22,3.33]"::vector $$) AS (n halfvec);
SELECT * FROM cypher('graph', $$ RETURN "[1.22,2.22,3.33]"::vector $$) AS (n sparsevec);

SELECT * FROM cypher('graph', $$ RETURN l2_distance("[1,2,3]", "[1,2,4]") $$) AS (n agtype);
SELECT * FROM cypher('graph', $$ RETURN inner_product("[1,2,3]", "[1,2,4]") $$) AS (n agtype);
SELECT * FROM cypher('graph', $$ RETURN cosine_distance("[1,2,3]", "[1,2,4]") $$) AS (n agtype);
SELECT * FROM cypher('graph', $$ RETURN l1_distance("[1,2,3]", "[1,2,4]") $$) AS (n agtype);
SELECT * FROM cypher('graph', $$ RETURN vector_dims("[1,2,3]") $$) AS (n agtype);
SELECT * FROM cypher('graph', $$ RETURN vector_norm("[1,2,3]") $$) AS (n agtype);
SELECT * FROM cypher('graph', $$ RETURN l2_normalize("[1,2,3]") $$) AS (n vector);
SELECT * FROM cypher('graph', $$ RETURN l2_normalize("[1,2,3]")::text $$) AS (n agtype);
SELECT * FROM cypher('graph', $$ RETURN subvector("[1,2,3,4,5,6]", 2, 4) $$) AS (n vector);
SELECT * FROM cypher('graph', $$ RETURN subvector("[1,2,3,4,5,6]", 2, 4)::text $$) AS (n agtype);
SELECT * FROM cypher('graph', $$ RETURN binary_quantize("[1,2,4]") $$) AS (n bit);

-- An example usage
SELECT * FROM cypher('graph', $$ 
    CREATE (:Movie {title: "The Matrix", year: 1999, genre: "Action", plot: "A computer hacker learns about the true nature of reality and joins a rebellion to free humanity from a simulated world controlled by machines.", embedding: "[-0.07594558, 0.04081754, 0.29592122, -0.11921061]"}),
           (:Movie {title: "The Matrix Reloaded", year: 2003, genre: "Action", plot: "The rebels continue their fight against the machines, uncovering deeper truths about the Matrix and the nature of their mission.", embedding: "[0.30228977, -0.22839354, 0.35070436, 0.01262819]"}),
           (:Movie {title: "The Matrix Revolutions", year: 2003, genre: "Action", plot: "The final battle between humans and machines reaches its climax as the fate of both worlds hangs in the balance.", embedding: "[ 0.12240622, -0.29752459, 0.22620453, 0.24454723]"}),
           (:Movie {title: "The Matrix Resurrections", year: 2021, genre: "Action", plot: "Neo returns to a new version of the Matrix and must once again fight to save the people from the control of the machines.", embedding: "[ 0.34717246, -0.13820869, 0.29214213, 0.08090488]"}),
           (:Movie {title: "Inception", year: 2010, genre: "Sci-Fi", plot: "A skilled thief is given a chance at redemption if he can successfully perform an inception: planting an idea into someone’s subconscious.", embedding: "[ 0.03923657, 0.39284106, -0.20927092, -0.17770818]"}),
           (:Movie {title: "Interstellar", year: 2014, genre: "Sci-Fi", plot: "A group of explorers travel through a wormhole in space in an attempt to ensure humanity’s survival.", embedding: "[-0.29302418, -0.39615033, -0.23393948, -0.09601383]"}),
           (:Movie {title: "Avatar", year: 2009, genre: "Sci-Fi", plot: "A paraplegic Marine is sent to the moon Pandora, where he becomes torn between following orders and protecting the world he feels is his home.", embedding: "[-0.13663386, 0.00635589, -0.03038832, -0.08252723]"}),
           (:Movie {title: "Blade Runner", year: 1982, genre: "Sci-Fi", plot: "A blade runner must pursue and terminate four replicants who have stolen a ship in space and returned to Earth.", embedding: "[ 0.27215557, -0.1479577, -0.09972772, -0.08234394]"}),
           (:Movie {title: "Blade Runner 2049", year: 2017, genre: "Sci-Fi", plot: "A new blade runner unearths a long-buried secret that has the potential to plunge what’s left of society into chaos.", embedding: "[ 0.21560573, -0.07505179, -0.01331814, 0.13403069]"}),
           (:Movie {title: "Minority Report", year: 2002, genre: "Sci-Fi", plot: "In a future where a special police unit can arrest murderers before they commit their crimes, a top officer is accused of a future murder.", embedding: "[ 0.24008012, 0.44954908, -0.30905488, 0.15195407]"}),
           (:Movie {title: "Total Recall", year: 1990, genre: "Sci-Fi", plot: "A construction worker discovers that his memories have been implanted and becomes embroiled in a conspiracy on Mars.", embedding: "[-0.17471036, 0.14695261, -0.06272433, -0.21795064]"}),
           (:Movie {title: "Elysium", year: 2013, genre: "Sci-Fi", plot: "In a future where the rich live on a luxurious space station while the rest of humanity lives in squalor, a man fights to bring equality.", embedding: "[-0.33280967, 0.07733926, 0.11015328, 0.53382836]"}),
           (:Movie {title: "Gattaca", year: 1997, genre: "Sci-Fi", plot: "In a future where genetic engineering determines social class, a man defies his fate to achieve his dreams.", embedding: "[-0.21629286, 0.31114665, 0.08303899, 0.46199759]"}),
           (:Movie {title: "The Fifth Element", year: 1997, genre: "Sci-Fi", plot: "In a futuristic world, a cab driver becomes the key to saving humanity from an impending cosmic threat.", embedding: "[-0.11528205, -0.0208782, -0.0735215, 0.14327449]"}),
           (:Movie {title: "The Terminator", year: 1984, genre: "Action", plot: "A cyborg assassin is sent back in time to kill the mother of the future resistance leader.", embedding: "[ 0.33666933, 0.18040994, -0.01075103, -0.11117851]"}),
           (:Movie {title: "Terminator 2: Judgment Day", year: 1991, genre: "Action", plot: "A reprogrammed Terminator is sent to protect the future leader of the human resistance from a more advanced Terminator.", embedding: "[ 0.34698868, 0.06439331, 0.06232323, -0.19534876]"}),
           (:Movie {title: "Jurassic Park", year: 1993, genre: "Adventure", plot: "Scientists clone dinosaurs to create a theme park, but things go awry when the creatures escape.", embedding: "[ 0.01794725, -0.11434246, -0.46831815, -0.01049593]"}),
           (:Movie {title: "The Avengers", year: 2012, genre: "Action", plot: "Superheroes assemble to face a global threat from an alien invasion led by Loki.", embedding: "[ 0.00546514, -0.37005171, -0.42612838, 0.07968612]"})
$$) AS (result agtype);
SELECT * FROM cypher('graph', $$ MATCH (m:Movie) RETURN m.title, (m.embedding)::vector $$) AS (title agtype, embedding vector);

-- Check the dimension of the embedding
SELECT * FROM cypher('graph', $$ MATCH (m:Movie) RETURN m.title, vector_dims(m.embedding) $$) AS (title agtype, dimension int);

-- Get top 4 most similar movies to The Terminator using cosine distance
SELECT * FROM cypher('graph', $$ MATCH (m:Movie), (search:Movie {title: "The Terminator"})
                                 RETURN m.title ORDER BY cosine_distance(m.embedding, search.embedding) ASC LIMIT 4
$$) AS (title agtype);
-- Get top 4 most similar movies to The Matrix using cosine distance
SELECT * FROM cypher('graph', $$ MATCH (m:Movie), (search:Movie {title: "The Matrix"})
                                 RETURN m.title ORDER BY cosine_distance(m.embedding, search.embedding) ASC LIMIT 4
$$) AS (title agtype);
-- l2 norm of the embedding
SELECT * FROM cypher('graph', $$ MATCH (m:Movie) set m.embedding=(l2_normalize(m.embedding))::text return m.title, m.embedding $$) AS (title agtype, embedding agtype);

-- Get top 4 most similar movies to The Terminator using l2 distance
SELECT * FROM cypher('graph', $$ MATCH (m:Movie), (search:Movie {title: "The Terminator"})
                                 RETURN m.title ORDER BY l2_distance(m.embedding, search.embedding) ASC LIMIT 4
$$) AS (title agtype);
-- Get top 4 most similar movies to The Matrix using l2 distance
SELECT * FROM cypher('graph', $$ MATCH (m:Movie), (search:Movie {title: "The Matrix"})
                                 RETURN m.title ORDER BY l2_distance(m.embedding, search.embedding) ASC LIMIT 4
$$) AS (title agtype);

SELECT drop_graph('graph', true);
DROP EXTENSION vector CASCADE;