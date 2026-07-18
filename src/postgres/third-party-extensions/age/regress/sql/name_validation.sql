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

SET search_path TO ag_catalog;

--
-- Test graph names
--

-- length

-- invalid (length < 3)
SELECT create_graph('db');
-- valid (though length > 63, it's truncated automatically before reaching validation function)
SELECT create_graph('oiblpsacrufgxiilyevvoiblpsacrufgxiilyevvoiblpsacrufgxiilyevvsdss');
-- valid
SELECT create_graph('mydatabase');

-- numeric characters

-- invalid (first character numeric; only alphabetic allowed)
SELECT create_graph('2mydatabase');
-- valid
SELECT create_graph('mydatabase2');

-- special characters

-- invalid (newline character)
SELECT create_graph('my
database');
-- invalid (space character)
SELECT create_graph('my database');
-- invalid (symbol character)
SELECT create_graph('my&database');
-- valid (non-ascii alphabet)
SELECT create_graph('mydätabase');
SELECT create_graph('mydঅtabase');

-- dots, dashes, underscore

-- valid
SELECT create_graph('main.db');
-- invalid (ends with dot)
SELECT create_graph('main.db.');
-- valid
SELECT create_graph('main-db');
-- invalid (ends with dash)
SELECT create_graph('main.db-');
-- valid
SELECT create_graph('_mydatabase');
SELECT create_graph('my_database');

-- test rename

-- invalid
SELECT alter_graph('mydatabase', 'RENAME', '1mydatabase');
-- valid
SELECT alter_graph('mydatabase', 'RENAME', 'mydatabase1');

-- clean up
SELECT drop_graph('mydatabase1', true);
SELECT drop_graph('mydätabase', true);
SELECT drop_graph('mydঅtabase', true);
SELECT drop_graph('mydatabase2', true);
SELECT drop_graph('main.db', true);
SELECT drop_graph('main-db', true);
SELECT drop_graph('_mydatabase', true);
SELECT drop_graph('my_database', true);
SELECT drop_graph('oiblpsacrufgxiilyevvoiblpsacrufgxiilyevvoiblpsacrufgxiilyevvsds', true);


--
-- Test label names
--

SELECT create_graph('graph123');

-- length

-- invalid
SELECT create_vlabel('graph123', '');
SELECT create_elabel('graph123', '');
-- valid
SELECT create_vlabel('graph123', 'labelx');
SELECT create_elabel('graph123', 'labely');

-- special characters

-- invalid (newline character)
SELECT create_vlabel('graph123', 'my
label');
SELECT create_elabel('graph123', 'my
label');
-- invalid (space character)
SELECT create_vlabel('graph123', 'my label');
SELECT create_elabel('graph123', 'my label');
-- invalid (symbol character)
SELECT create_vlabel('graph123', 'my&label');
SELECT create_elabel('graph123', 'my&label');
-- valid (non-ascii alphabet)
SELECT create_vlabel('graph123', 'myläbelx');
SELECT create_elabel('graph123', 'myläbely');
SELECT create_vlabel('graph123', 'mylঅbelx');
SELECT create_elabel('graph123', 'mylঅbely');
-- valid (underscore)
SELECT create_vlabel('graph123', '_labelx');
SELECT create_elabel('graph123', '_labely');
SELECT create_vlabel('graph123', 'label_x');
SELECT create_elabel('graph123', 'label_y');

-- numeric

-- invalid
SELECT create_vlabel('graph123', '1label');
SELECT create_elabel('graph123', '2label');
-- valid
SELECT create_vlabel('graph123', 'label1');
SELECT create_elabel('graph123', 'label2');

-- label creation with cypher

-- invalid
SELECT * from cypher('graph123', $$ CREATE (a:`my&label`) $$) as (a agtype);
SELECT * from cypher('graph123', $$ CREATE (:A)-[:`my&label2`]->(:C) $$) as (a agtype);

-- valid
SELECT * from cypher('graph123', $$ CREATE (a:`mylabel`) $$) as (a agtype);
SELECT * from cypher('graph123', $$ CREATE (:A)-[:`mylabel2`]->(:C) $$) as (a agtype);

-- user label validation
-- invalid
SELECT * from cypher('graph123', $$ return is_valid_label_name('1label') $$) as (result agtype);
SELECT * from cypher('graph123', $$ return is_valid_label_name('2label') $$) as (result agtype);
-- valid
SELECT * from cypher('graph123', $$ return is_valid_label_name('label1') $$) as (result agtype);
SELECT * from cypher('graph123', $$ return is_valid_label_name('label2') $$) as (result agtype);

-- issue 1986: label name validation of long names.
-- Label names are relation names which are restricted to NAMEDATALEN-1 in size.
-- However, we can't validate PG type Names due to namein() truncating anything
-- over NAMEDATALEN-1. To allow the label names to be checked over NAMEDATELEN-1
-- we changed the input type from PG's Name to cstring. These checks are to
-- verify that these can now be caught.
--
-- should return false and a warning.
SELECT * from cypher('graph123', $$ return is_valid_label_name('label01234567890123456789012345678901234567890123456789012345678') $$) as (result agtype);
-- should be successful
SELECT * from cypher('graph123', $$ return is_valid_label_name('label0123456789012345678901234567890123456789012345678901234567') $$) as (result agtype);
--
-- now check vlabel creation, should fail
SELECT create_vlabel('graph123', 'vlabel01234567890123456789012345678901234567890123456789012345678901234567890123456789012345678');
-- should be successful
SELECT create_vlabel('graph123', 'vlabel012345678901234567890123456789012345678901234567890123456');
--
-- now check elabel creation, should fail
SELECT create_elabel('graph123', 'elabel0123456789012345678901234567890123456789012345678901234567');
-- should be okay
SELECT create_elabel('graph123', 'elabel012345678901234567890123456789012345678901234567890123456');

-- clean up
SELECT drop_graph('graph123', true);

--
-- Test GUC names
--
SET age.enable_containment TO ON;
SET age.invalid_parameter TO ON;
SET any_placeholder.any_parameter TO ON;

--
-- End of test
--
