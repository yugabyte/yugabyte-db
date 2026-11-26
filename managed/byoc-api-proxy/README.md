# BYOC API proxy
This is a simple HTTP relay app pulling queued requests and proxying them to a YBA instance.
For use in restricted BYOC deployments where direct calls to YBA from Aeon infra are not possible.

## Validate configuration
To check env / YAML against the same Bean Validation constraints and auth rules the app uses at
startup — reporting **all** problems instead of failing on the first `@ConfigurationProperties`
bean — run:

```bash
java -jar byoc-api-proxy.jar --validate-config
```

Optional Spring config locations still apply, for example:

```bash
java -jar byoc-api-proxy.jar --validate-config \
  --spring.config.additional-location=optional:file:/etc/yugabyte/byoc-api-proxy/application.yaml
```

Exit code `0` means the configuration is valid; `1` means one or more errors were printed.

## Use with Aeon API Server
Aeon API server uses a 'service account' whitelist to authenticate internal APIs.
It expects a JSON file of the following format
```json
[
  {
    "email": "cloud-admin-console-dev@yugabyte.com",
    "password": "random-password-1"
  }
]
```
These creds should be used in the `auth.service_account` section of the app config.

## Container image and Helm chart
`docker/build-image.sh` builds the image from a release tarball, so the image runs the published
jar. Configuration is the same environment variables as `src/main/resources/application.yaml`.

```bash
./release.sh -l
./docker/build-image.sh -t build/releases/byoc_api_proxy-<version>.tar.gz -r byoc-api-proxy
```

`chart/` deploys it as a single-replica Deployment (a second poller would re-run requests that
outlast the API server's 5 s claim lease). As a
yugaware sub-chart it defaults to that release's YBA service; standalone, set `yba.baseUrl`.
`chart/package.sh <version> <dir>` packages it with the chart version set to the proxy version.
