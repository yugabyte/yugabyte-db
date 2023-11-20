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
 
--
-- agtype GIN support
--
CREATE FUNCTION ag_catalog.gin_compare_agtype(text, text)
    RETURNS int
AS 'MODULE_PATHNAME'
LANGUAGE C
IMMUTABLE
STRICT
PARALLEL SAFE;

CREATE FUNCTION gin_extract_agtype(agtype, internal)
    RETURNS internal
AS 'MODULE_PATHNAME'
LANGUAGE C
IMMUTABLE
STRICT
PARALLEL SAFE;

CREATE FUNCTION ag_catalog.gin_extract_agtype_query(agtype, internal, int2,
                                                    internal, internal)
    RETURNS internal
AS 'MODULE_PATHNAME'
LANGUAGE C
IMMUTABLE
STRICT
PARALLEL SAFE;

CREATE FUNCTION ag_catalog.gin_consistent_agtype(internal, int2, agtype, int4,
                                                 internal, internal)
    RETURNS bool
AS 'MODULE_PATHNAME'
LANGUAGE C
IMMUTABLE
STRICT
PARALLEL SAFE;

CREATE FUNCTION ag_catalog.gin_triconsistent_agtype(internal, int2, agtype, int4,
                                                    internal, internal, internal)
    RETURNS bool
AS 'MODULE_PATHNAME'
LANGUAGE C
IMMUTABLE
STRICT
PARALLEL SAFE;

CREATE OPERATOR CLASS ag_catalog.gin_agtype_ops
DEFAULT FOR TYPE agtype USING gin AS
  OPERATOR 7 @>(agtype, agtype),
  OPERATOR 8 <@(agtype, agtype),
  OPERATOR 9 ?(agtype, agtype),
  OPERATOR 10 ?|(agtype, agtype),
  OPERATOR 11 ?&(agtype, agtype),
  FUNCTION 1 ag_catalog.gin_compare_agtype(text,text),
  FUNCTION 2 ag_catalog.gin_extract_agtype(agtype, internal),
  FUNCTION 3 ag_catalog.gin_extract_agtype_query(agtype, internal, int2,
                                                 internal, internal),
  FUNCTION 4 ag_catalog.gin_consistent_agtype(internal, int2, agtype, int4,
                                              internal, internal),
  FUNCTION 6 ag_catalog.gin_triconsistent_agtype(internal, int2, agtype, int4,
                                                 internal, internal, internal),
STORAGE text;
