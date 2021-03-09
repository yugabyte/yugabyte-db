import {types, Client, QueryResultRow} from "pg";
import {setAGETypes} from "../src";

const config = {
    user: 'postgres',
    host: '127.0.0.1',
    database: 'postgres',
    password: 'postgres',
    port: 25432,
}

const testGraphName = 'age-test';

describe("Pre-connected Connection", () => {
    let client: Client | null;

    beforeAll(async () => {
        client = new Client(config);
        await client.connect();
        await setAGETypes(client, types);
        await client.query(`SELECT create_graph('${testGraphName}');`);
    });
    afterAll(async () => {
        await client?.query(`SELECT drop_graph('${testGraphName}', true);`);
        await client?.end();
    })
    it("simple CREATE & MATCH", async () => {
        await client?.query(`
            SELECT *
            from cypher('${testGraphName}', $$ CREATE (a:Part {part_num: '123'}),
                        (b:Part {part_num: '345'}),
                        (c:Part {part_num: '456'}),
                        (d:Part {part_num: '789'})
                            $$) as (a agtype);
        `)
        const results: QueryResultRow = await client?.query<QueryResultRow>(`
            SELECT *
            from cypher('${testGraphName}', $$
                MATCH (a) RETURN a
            $$) as (a agtype);
        `)!
        expect(results.rows).toStrictEqual(
            [{
                "a": {
                    "id": 844424930131969,
                    "label": "Part",
                    "properties": {"part_num": "123"}
                }
            }, {
                "a": {
                    "id": 844424930131970,
                    "label": "Part",
                    "properties": {"part_num": "345"}
                }
            }, {
                "a": {
                    "id": 844424930131971,
                    "label": "Part",
                    "properties": {"part_num": "456"}
                }
            }, {"a": {"id": 844424930131972, "label": "Part", "properties": {"part_num": "789"}}}]
        )
    });
});
