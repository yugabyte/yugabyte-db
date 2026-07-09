---
title: YugabyteDB Anywhere REST API
headerTitle: YugabyteDB Anywhere REST API
linkTitle: REST API
description: REST API YugabyteDB Anywhere.
headcontent: Manage YugabyteDB Anywhere accounts and deployments using the REST API
menu:
  v2025.1_yugabyte-platform:
    parent: anywhere-automation
    identifier: anywhere-api
    weight: 10
type: docs
---

The YugabyteDB Anywhere REST API allows you to deploy and manage universes programmatically. Some examples of what you can accomplish using this API include:

- Create and manage YugabyteDB Anywhere provider configurations
- Deploy and manage universes
- Pause and resume universes
- Perform software upgrades
- Run on-demand backups and restores
- Resize nodes

If you use Python, check out the Jupyter notebooks in the [yugabyte-db GitHub repository](https://github.com/yugabyte/yugabyte-db/tree/master/managed/api-examples) for examples of performing various tasks using the API.

## Change placement and replication factor

Use one of the following patterns. For **v1**, the request body is the same shape as the `universeDetails` object on a universe **GET** response: you copy it, change the **primary** cluster’s `userIntent` (for example `replicationFactor` and `regionList`) and `placementInfo` as needed, and submit the updated JSON. For **v2**, the body is a smaller [UniverseEditSpec](https://github.com/yugabyte/yugabyte-db/blob/master/managed/src/main/resources/openapi/components/schemas/UniverseEditSpec.yaml) (snake_case field names in JSON), not a full GET payload.

**Authentication:** send your API token in the `X-AUTH-YW-API-TOKEN` header (same as other v1 API calls), unless your installation uses a different header for v2; see the [YugabyteDB Anywhere API reference](https://api-docs.yugabyte.com/docs/yugabyte-platform/f10502c9c9623-yugabyte-db-anywhere-api-overview).

### Update primary cluster (v1, recommended)

`PUT` `/api/v1/customers/{cUUID}/universes/{uniUUID}/clusters/primary`

Applies an edit to the **primary** cluster (placement, replication factor, node count, and related fields) and returns a **task** UUID. Example:

```sh
curl --request PUT \
  "https://<yba-host>/api/v1/customers/<cUUID>/universes/<uniUUID>/clusters/primary" \
  --header "X-AUTH-YW-API-TOKEN: <api_token>" \
  --header "Content-Type: application/json" \
  --data-binary @universe-primary-edit.json
```

- **GET** the universe first: `GET` `/api/v1/customers/{cUUID}/universes/{uniUUID}` and use `universeDetails` as the base of `universe-primary-edit.json`.
- In the primary cluster object, set `userIntent.replicationFactor` and update `placementInfo` (and any other required fields) to match the desired layout.
- For a step-by-step example, see the [edit universe notebook](https://github.com/yugabyte/yugabyte-db/blob/master/managed/api-examples/python-simple/edit-universe.ipynb).

**Optional: validate or compute configuration before apply:** `POST` `/api/v1/customers/{cUUID}/universe_configure` with a similar `UniverseConfigureTaskParams` body (used for configure/validation flows in the product).

**Legacy: full universe `PUT`:** `PUT` `/api/v1/customers/{cUUID}/universes/{uniUUID}` (older single-endpoint update). Prefer `.../clusters/primary` for primary cluster changes.

### Edit universe (v2, partial spec)

`PUT` `/api/v2/customers/{cUUID}/universes/{uniUUID}`

Body: **UniverseEditSpec** with at least `expected_universe_version` (use `-1` to skip version checking) and a `clusters` array of **ClusterEditSpec** entries. Each cluster edit only includes fields you want to change, such as `uuid` (required), `num_nodes`, `provider_spec` (`region_list`), `placement_spec` (`cloud_list`), `node_spec`, and `instance_tags`. Property names in JSON are **snake_case** (for example `placement_spec`, not `placementInfo`).

Do **not** send a full v1-style universe `GET` document here; the v2 contract is different and extra fields (such as `clusterType` on a cluster) are rejected.

```sh
curl --request PUT \
  "https://<yba-host>/api/v2/customers/<cUUID>/universes/<uniUUID>" \
  --header "X-AUTH-YW-API-TOKEN: <api_token>" \
  --header "Content-Type: application/json" \
  --data-binary @universe-edit-v2.json
```

The OpenAPI definitions in the [managed/src/main/resources/openapi](https://github.com/yugabyte/yugabyte-db/tree/master/managed/src/main/resources/openapi) directory are the source of truth for v2 request shapes.

{{< sections/2-boxes >}}
  {{< sections/bottom-image-box
    title="Get Started"
    description="Manage YugabyteDB Anywhere using the API."
    buttonText="API Documentation"
    buttonUrl="https://api-docs.yugabyte.com/docs/yugabyte-platform/f10502c9c9623-yugabyte-db-anywhere-api-overview"
  >}}

{{< /sections/2-boxes >}}
