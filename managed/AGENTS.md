# AGENTS.md: YugabyteDB Anywhere (managed/)

## v2 REST API

The v2 API is defined in OpenAPI and its Play controllers are generated. API clients include
Aeon, Terraform, the CLI and customer scripts. Each depends on the response codes and payload
shapes, so treat every change as a contract change.

### Where things go

- Spec: `src/main/resources/openapi/`. Put schemas in `components/schemas/`, bodies in
  `components/requestBodies/` and `components/responses/`, and operations in
  `paths/<resource>.yaml`. Add a new file for a new resource.
- Generated, never edited by hand: `paths/_index.yaml`, the bundled `openapi.yaml`, and
  everything under `target/openapi/`. `openapi_public.yaml` is generated too, but committed.
- After a spec change, run `sbt cleanV2ServerStubs openApiProcessServer compile`, then commit
  the regenerated `paths/_index.yaml` and `openapi_public.yaml`. sbt's change tracking can miss
  spec changes, so don't skip `cleanV2ServerStubs`.
- Bump the `yba-client-v2` version in `client/java/v2/pom.xml` and `build.sbt` in the same
  change; lint (`managed/lint/check-client-version.sh`) fails otherwise.
- Implement the generated `<Tag>ApiControllerImpInterface` as `<Tag>ApiControllerImp` in
  `src/main/java/api/v2/controllers/`. Keep it thin: it calls an injected handler in
  `api/v2/handlers/`, which holds the logic and calls the existing v1 handlers and models.
- Convert between v2 and v1 types with MapStruct mappers in `api/v2/mappers/` (see the
  `README.txt` there). They use `CentralConfig`, and their methods are named `to<TargetType>`,
  prefixed `toV2`/`toV1` when both sides share a type name. Check the generated mapper under
  `target/scala-2.13/classes/api/v2/mappers/` and unit-test the mapping.

### Contract rules

- A resource is `{spec, info}`: `spec` is what the client sets (the create body), `info` is what
  YBA generates. Requests and responses are typed schemas, never free-form JSON.
- Status codes: 404 for a resource that doesn't exist, not 400. Many v1 and older v2 handlers
  use `getOrBadRequest`, which answers 400; don't copy it into new endpoints. Use 401/403 for
  auth, 409 for a conflict with current state, 400 for an invalid request. Clients must not
  need to parse error messages to tell cases apart.
- Changes are additive only: a new operation, a new optional property, or a required property
  made optional. Renaming, moving, removing, retyping, or making something required breaks
  clients. Deprecate instead (`x-yba-api-visibility: deprecated` plus `x-yba-api-since`), and
  remove after two major releases.
- New operations and schemas start as `x-yba-api-visibility: preview` with
  `x-yba-api-since: <next YBA version>`. YBM-only ones are `internal`, which keeps them out of
  `openapi_public.yaml`.
- Mark every operation that creates a resource or submits a task with `x-yba-api-idempotent:
  true`, whatever its HTTP method. The build adds the `Idempotency-Key` header and the 409/422
  responses; don't declare them yourself. Leave it off operations whose response carries a
  secret, since the response is stored for replay.
- Paged lists are `POST <collection>/page` with a `<Type>PagedQuerySpec` body and a
  `<Type>PagedResp` response.
- Property names are snake_case and path segments are hyphen-case. The style validator
  (`openapi_style_validator.conf`) enforces this, and also requires descriptions on operations
  and properties.
- Schema descriptions start with the schema name on its own line and end with
  `Part of <request or response schema>`, so the API docs can be navigated.
- Enum value names share one namespace in the Go client. Give a new enum `x-enum-varnames` with
  type-prefixed names to avoid collisions.
- All vendor extensions (`x-yba-api-*`, authz, audit, multipart, conditional GET, idempotency)
  are documented in `src/main/resources/openapi_templates/server/README.txt`. Read it before
  adding an operation.

### Tests

Test v2 endpoints through the generated Java client (`com.yugabyte.yba.v2.client`), as the
tests in `src/test/java/com/yugabyte/yw/api/v2/` do. That exercises the generated
serialization, not only the handler.
