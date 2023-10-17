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

/*
 * This module provides functions for directly building agtype
 * without using agtype_value.
 */

#ifndef AG_AGTYPE_RAW_H
#define AG_AGTYPE_RAw_H

#include "postgres.h"
#include "utils/agtype.h"
#include "utils/agtype_ext.h"

/*
 * We declare the agtype_build_state here, and in this way, so that it may be
 * used elsewhere. However, we keep the contents private by defining it in
 * agtype_raw.c
 */
typedef struct agtype_build_state agtype_build_state;

agtype_build_state *init_agtype_build_state(uint32 size, uint32 header_flag);
agtype *build_agtype(agtype_build_state *bstate);
void pfree_agtype_build_state(agtype_build_state *bstate);

void write_string(agtype_build_state *bstate, char *str);
void write_graphid(agtype_build_state *bstate, graphid graphid);
void write_container(agtype_build_state *bstate, agtype *agtype);
void write_extended(agtype_build_state *bstate, agtype *val, uint32 header);

#endif
