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

#include "funcapi.h"

#include <unistd.h>
#include "utils/age_session_info.h"

/*
 * static/global session info variables for use with the driver interface.
 */
static int session_info_pid = -1;
static char *session_info_graph_name = NULL;
static char *session_info_cypher_statement = NULL;
static bool session_info_prepared = false;

static void set_session_info(char *graph_name, char *cypher_statement);

/* function to set the session info. it will clean it, if necessary. */
static void set_session_info(char *graph_name, char *cypher_statement)
{
    MemoryContext oldctx = NULL;

    if (is_session_info_prepared())
    {
        reset_session_info();
    }

    /* we need to use a higher memory context for the data pointed to. */
    oldctx = MemoryContextSwitchTo(TopMemoryContext);

    if (graph_name != NULL)
    {
        session_info_graph_name = pstrdup(graph_name);
    }
    else
    {
        session_info_graph_name = NULL;
    }

    if (cypher_statement != NULL)
    {
        session_info_cypher_statement = pstrdup(cypher_statement);
    }
    else
    {
        session_info_cypher_statement = NULL;
    }

    /* switch back to the original context */
    MemoryContextSwitchTo(oldctx);

    session_info_pid = getpid();
    session_info_prepared = true;
}

/*
 * Helper function to return the value of session_info_cypher_statement or NULL
 * if the value isn't set. The value returned is a copy, so please free it when
 * done.
 */
char *get_session_info_graph_name(void)
{
    if (is_session_info_prepared() &&
        session_info_graph_name != NULL)
    {
        return pstrdup(session_info_graph_name);
    }

    return NULL;
}

/*
 * Helper function to return the value of session_info_cypher_statement or NULL
 * if the value isn't set. The value returned is a copy, so please free it when
 * done.
 */
char *get_session_info_cypher_statement(void)
{
    if (is_session_info_prepared() &&
        session_info_cypher_statement != NULL)
    {
        return pstrdup(session_info_cypher_statement);
    }

    return NULL;
}

/* function to return the state of the session info data */
bool is_session_info_prepared(void)
{
    /* is the session info prepared AND is the pid the same pid */
    if (session_info_prepared == true &&
        session_info_pid == getpid())
    {
        return true;
    }

    return false;
}

/* function to clean and reset the session info back to default values */
void reset_session_info(void)
{
    /* if the session info is prepared, free the strings */
    if (session_info_prepared == true)
    {
        if (session_info_graph_name != NULL)
        {
            pfree_if_not_null(session_info_graph_name);
        }

        if (session_info_cypher_statement != NULL)
        {
            pfree_if_not_null(session_info_cypher_statement);
        }
    }

    /* reset the session info back to default unused values */
    session_info_graph_name = NULL;
    session_info_cypher_statement = NULL;
    session_info_prepared = false;
    session_info_pid = -1;
}

/* AGE SQL function to prepare session info */
PG_FUNCTION_INFO_V1(age_prepare_cypher);

Datum age_prepare_cypher(PG_FUNCTION_ARGS)
{
    char *graph_name_str = NULL;
    char *cypher_statement_str = NULL;

    /* both arguments must be non-NULL */
    if (PG_ARGISNULL(0) || PG_ARGISNULL(1))
    {
        PG_RETURN_BOOL(false);
    }

    graph_name_str = PG_GETARG_CSTRING(0);
    cypher_statement_str = PG_GETARG_CSTRING(1);

    /* both strings must be non-NULL */
    if (graph_name_str == NULL || cypher_statement_str == NULL)
    {
        PG_RETURN_BOOL(false);
    }

    set_session_info(graph_name_str, cypher_statement_str);

    PG_RETURN_BOOL(true);
}
