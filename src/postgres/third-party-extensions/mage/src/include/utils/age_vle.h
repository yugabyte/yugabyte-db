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

#ifndef AG_AGTYPE_VLE_H
#define AG_AGTYPE_VLE_H

#include "utils/agtype.h"
#include "utils/age_global_graph.h"

/*
 * We declare the VLE_path_container here, and in this way, so that it may be
 * used elsewhere. However, we keep the contents private by defining it in
 * agtype_vle.c
 */
typedef struct VLE_path_container VLE_path_container;

/*
 * Function to take an AGTV_BINARY VLE_path_container and return a path as an
 * agtype.
 */
agtype *agt_materialize_vle_path(agtype *agt_arg_vpc);
/*
 * Function to take a AGTV_BINARY VLE_path_container and return a path as an
 * agtype_value.
 */
agtype_value *agtv_materialize_vle_path(agtype *agt_arg_vpc);
/*
 * Exposed helper function to make an agtype_value AGTV_ARRAY of edges from a
 * VLE_path_container.
 */
agtype_value *agtv_materialize_vle_edges(agtype *agt_arg_vpc);

#endif
