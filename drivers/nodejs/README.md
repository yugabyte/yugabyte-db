nodejs-pg-age
===========



Example
-----
Initialize on make connection

```typescript
import {types, Client, QueryResultRow} from "pg";
import {setAGETypes} from "../src";

const config = {
    user: 'postgres',
    host: '127.0.0.1',
    database: 'postgres',
    password: 'postgres',
    port: 25432,
}

const client = new Client(config);
await client.connect();
await setAGETypes(client, types);

await client.query(`SELECT create_graph('age-first-time');`);
```

Query

```typescript
await client?.query(`
    SELECT *
    from cypher('age-first-time', $$
        CREATE (a:Part {part_num: '123'}),
            (b:Part {part_num: '345'}),
            (c:Part {part_num: '456'}),
            (d:Part {part_num: '789'})
    $$) as (a agtype);
`)

const results: QueryResultRow = await client?.query<QueryResultRow>(`
    SELECT *
    from cypher('age-first-time', $$
        MATCH (a) RETURN a
    $$) as (a agtype);
`)!
```
