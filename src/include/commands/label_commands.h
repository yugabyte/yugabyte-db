/*
 * Copyright 2020 Bitnine Co., Ltd.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef AG_LABEL_COMMANDS_H
#define AG_LABEL_COMMANDS_H

#include "postgres.h"

#define LABEL_TYPE_VERTEX 'v'
#define LABEL_TYPE_EDGE 'e'

#define AG_DEFAULT_LABEL_EDGE "_ag_label_edge"
#define AG_DEFAULT_LABEL_VERTEX "_ag_label_vertex"

#define IS_AG_DEFAULT_LABEL(x) \
    (!strcmp(x, AG_DEFAULT_LABEL_EDGE) || !strcmp(x, AG_DEFAULT_LABEL_VERTEX))

Oid create_label(char *graph_name, char *label_name, char label_type,
                 List *parents);

#endif
