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

#ifndef AG_AGE_GRAPHID_DS_H
#define AG_AGE_GRAPHID_DS_H

#include "utils/graphid.h"
#include "utils/agtype.h"

#define IS_GRAPHID_STACK_EMPTY(stack) \
            get_stack_size(stack) == 0
#define PEEK_GRAPHID_STACK(stack) \
            (graphid) get_graphid(peek_stack_head(stack))

/*
 * We declare the GRAPHID data structures here, and in this way, so that they
 * may be used elsewhere. However, we keep the contents private by defining them
 * in age_graphid_ds.c
 */

/* declare the GraphIdNode */
typedef struct GraphIdNode GraphIdNode;

/* declare the ListGraphId container */
typedef struct ListGraphId ListGraphId;

/* GraphIdNode access functions */
GraphIdNode *next_GraphIdNode(GraphIdNode *node);
graphid get_graphid(GraphIdNode *node);

/* graphid stack functions */
/* create a new ListGraphId stack */
ListGraphId *new_graphid_stack(void);
/* free a ListGraphId stack */
void free_graphid_stack(ListGraphId *stack);
/* push a graphid onto a ListGraphId stack */
void push_graphid_stack(ListGraphId *stack, graphid id);
/* pop (remove) a GraphIdNode from the top of the stack */
graphid pop_graphid_stack(ListGraphId *stack);
/* peek (doesn't remove) at the head entry of a ListGraphId stack */
GraphIdNode *peek_stack_head(ListGraphId *stack);
/* peek (doesn't remove) at the tail entry of a ListGraphId stack */
GraphIdNode *peek_stack_tail(ListGraphId *stack);
/* return the size of a ListGraphId stack */
int64 get_stack_size(ListGraphId *stack);

/* graphid list functions */
/*
 * Helper function to add a graphid to the end of a ListGraphId container.
 * If the container is NULL, it creates the container with the entry.
 */
ListGraphId *append_graphid(ListGraphId *container, graphid id);
/* free a ListGraphId container */
void free_ListGraphId(ListGraphId *container);
/* return a reference to the head entry of a list */
GraphIdNode *get_list_head(ListGraphId *list);
/* get the size of the passed list */
int64 get_list_size(ListGraphId *list);

#endif
