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

#include "postgres.h"

#include "parser/cypher_transform_entity.h"

// creates a transform entity
transform_entity *make_transform_entity(cypher_parsestate *cpstate,
                                        enum transform_entity_type type,
                                        Node *node, Expr *expr)
{
    transform_entity *entity;

    entity = palloc(sizeof(transform_entity));

    entity->type = type;
    if (type == ENT_VERTEX)
    {
        entity->entity.node = (cypher_node *)node;
    }
    else if (entity->type == ENT_EDGE || entity->type == ENT_VLE_EDGE)
    {
        entity->entity.rel = (cypher_relationship *)node;
    }
    else if (entity->type == ENT_PATH)
    {
        entity->entity.path = (cypher_path *)node;
    }
    else
    {
        ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
                        errmsg("unknown entity type")));
    }

    entity->declared_in_current_clause = true;
    entity->expr = expr;
    entity->in_join_tree = expr != NULL;

    return entity;
}

/*
 * Finds the transform_entity in the cypher_parstate for a the given name and
 * type.
 */
transform_entity *find_transform_entity(cypher_parsestate *cpstate,
                                        char *name,
                                        enum transform_entity_type type)
{
    ListCell *lc;

    if (name == NULL)
    {
        return NULL;
    }

    foreach(lc, cpstate->entities)
    {
        transform_entity *entity = lfirst(lc);

        if (entity->type != type)
        {
            continue;
        }

        if (type == ENT_VERTEX)
        {
            if (entity->entity.node->name != NULL && !strcmp(entity->entity.node->name, name))
            {
                return entity;
            }
        }
        else if (type == ENT_EDGE || type == ENT_VLE_EDGE)
        {
            if (entity->entity.rel->name != NULL && !strcmp(entity->entity.rel->name, name))
            {
                return entity;
            }
        }
        else if (type == ENT_PATH)
        {
            if (entity->entity.path->var_name != NULL && !strcmp(entity->entity.path->var_name, name))
            {
                return entity;
            }
        }
    }

    return NULL;
}

/*
 * Iterate through the cypher_parsestate's transform_entities and returns
 * the entity with name passed by name variable.
 */
transform_entity *find_variable(cypher_parsestate *cpstate, char *name)
{
    ListCell *lc;

    foreach (lc, cpstate->entities)
    {
        transform_entity *entity = lfirst(lc);
        char *entity_name;

        if (entity->type == ENT_VERTEX)
        {
            entity_name = entity->entity.node->name;
        }
        else if (entity->type == ENT_EDGE || entity->type == ENT_VLE_EDGE)
        {
            entity_name = entity->entity.rel->name;
        }
        else if (entity->type == ENT_PATH)
        {
            entity_name = entity->entity.path->var_name;
        }
        else
        {
            ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
                            errmsg("unknown entity type")));
        }

        if (entity_name != NULL && !strcmp(name, entity_name))
        {
            return entity;
        }
    }

    return NULL;
}

// helper function that extracts the name associated with the transform_entity.
char *get_entity_name(transform_entity *entity)
{
    if (entity->type == ENT_EDGE || entity->type == ENT_VLE_EDGE)
    {
        return entity->entity.rel->name;
    }
    else if (entity->type == ENT_VERTEX)
    {
        return entity->entity.node->name;
    }
    else if (entity->type == ENT_PATH)
    {
        return entity->entity.path->var_name;
    }
    else
    {
        ereport(ERROR,
                (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
                 errmsg("cannot get entity name from transform_entity type %i",
                        entity->type)));
    }

    return NULL;
}

/*
 * Returns entity->expr relative to the current cpstate.
 *
 * For example,
 * If entity is from current cpstate, its levelsup = 0.
 * If entity is from immediate parent cpstate, its levelsup = 1.
 * If entity is from parent's parent's cpstate, its levelsup = 2.
 *
 * Relative Expr is necessary when entity->expr is a Var and the entity
 * is not from the current cpstate. In this case, Var->varlevelsup must
 * reflect the distance between source cpstate of the entity and the
 * cpstate where the Var is being used.
 */
Expr *get_relative_expr(transform_entity *entity, Index levelsup)
{
    Var *var;
    Var *updated_var;

    if (!IsA(entity->expr, Var))
    {
        return entity->expr;
    }

    var = (Var *)entity->expr;
    updated_var = makeVar(var->varno, var->varattno, var->vartype,
                          var->vartypmod, var->varcollid,
                          var->varlevelsup + levelsup);

    return (Expr *)updated_var;
}
