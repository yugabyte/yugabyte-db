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

import {Client} from 'pg';
import pgTypes from 'pg-types'

function AGTypeParse(input: string) {
    // todo: need to work with antlr4

    const inputLength = input.length;
    let splitJson;
    if (input.endsWith("::edge")) {
        splitJson = input.substring(0, inputLength - 6)
    } else {
        // ::vertex
        splitJson = input.substring(0, inputLength - 8);
    }
    return JSON.parse(splitJson);
}

async function setAGETypes(client: Client, types: typeof pgTypes) {
    await client.query(`
        CREATE EXTENSION IF NOT EXISTS age;
        LOAD 'age';
        SET search_path = ag_catalog, "$user", public;
    `)

    const oidResults = await client.query(`
        select typelem
        from pg_type
        where typname = '_agtype';`);

    if (oidResults.rows.length < 1)
        throw new Error();

    types.setTypeParser(oidResults.rows[0].typelem, AGTypeParse)
}


export {setAGETypes, AGTypeParse}
