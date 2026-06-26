---
title: SuperAdmin LDAP and SSO user support in YugabyteDB Anywhere
headerTitle: SuperAdmin LDAP/SSO user support
linkTitle: SuperAdmin LDAP/SSO users
description: Assign SuperAdmin privileges to LDAP and OIDC users through controlled RBAC and group mapping flows in YugabyteDB Anywhere.
headcontent: Provision SuperAdmin access for LDAP and SSO users
menu:
  stable_yugabyte-platform:
    identifier: superadmin-ldap-sso-users
    parent: administer-yugabyte-platform
    weight: 25
type: docs
---

{{<tags/feature/ea idea="2631">}} Available in YugabyteDB Anywhere v2025.2.x and later.

YugabyteDB Anywhere supports LDAP and SSO (OIDC) users becoming SuperAdmin through controlled RBAC and group mapping flows.

This capability is guarded by a global runtime configuration flag, `yb.security.allow_superadmin_user_group_mapping` (default `false`). When enabled, it allows SuperAdmin assignment in v2 auth group mappings and role-binding APIs, with additional caller checks.

YugabyteDB Anywhere also includes security guardrails around user deletion and fixes for RBAC resolution issues affecting LDAP/SSO-derived SuperAdmin users.

## Enable or disable the feature

| Setting | Value |
| :------ | :---- |
| Runtime config key | `yb.security.allow_superadmin_user_group_mapping` |
| Scope | Global |
| Default | `false` |

Set the flag to `true` to allow the following:

- SuperAdmin in v2 LDAP/OIDC group mappings.
- SuperAdmin assignment via the role binding API, only when the caller is authenticated as SuperAdmin.

Keep this flag disabled unless you explicitly require this behavior. For instructions on changing global runtime configuration settings, refer to [Manage runtime configuration settings](../manage-runtime-config/).

## Unsupported scenarios

The following limitations apply even when the feature is enabled:

- Legacy and deprecated v1 LDAP/OIDC group mapping APIs continue to reject SuperAdmin mappings.
- Local SuperAdmin users are blocked from the SSO callback login path. SSO-provisioned SuperAdmin users (for example, LDAP/OIDC-derived) can still sign in through SSO.
- SuperAdmin assignment is not open-ended. API-side authorization checks still apply, and the caller must be SuperAdmin when required.

## Feature details

### Prerequisites

- LDAP or OIDC configured in YugabyteDB Anywhere. Refer to [Configure authentication for YugabyteDB Anywhere](../ldap-authentication/) and [OIDC authentication](../oidc-authentication/).
- SuperAdmin access to global runtime configuration settings.
- Understanding of RBAC role bindings, including direct and group-derived bindings. Refer to [Manage YugabyteDB Anywhere users](../anywhere-rbac/).

### Core behavior

When `yb.security.allow_superadmin_user_group_mapping` is enabled:

- The v2 group mapping flow validates roles with awareness of this flag.
- `RBACController.setRoleBindings` allows SuperAdmin role assignment only when the global flag is enabled **and** the authenticated caller is SuperAdmin.
- `SessionController.thirdPartyLogin` blocks SSO login for local SuperAdmin users only. Non-local SuperAdmin users (for example, LDAP/SSO-provisioned) can still sign in.
- UI role and mapping flows show or hide SuperAdmin options based on the flag and effective permissions.

### Security hardening

- Admin users can no longer delete SuperAdmin users through the user delete API.
- SuperAdmin self-delete is blocked in the UI (the delete action is disabled for your own account).
- Deletion checks use role-aware logic (including the new RBAC path), replacing older primary-user assumptions.

### RBAC and authentication fixes

- Group-derived role bindings are correctly considered when checking whether a user has the SuperAdmin role.
- Session and OAuth authentication resolution was fixed for SuperAdmin checks in relevant code paths.
- Global runtime configuration scope verification was corrected for UUID equality checks.
- UI group management permissions now derive SuperAdmin state from RBAC permissions instead of the legacy role field.

OpenAPI specifications were regenerated to align API documentation and tests with these behavior updates.

## Example workflow

1. Enable `yb.security.allow_superadmin_user_group_mapping` globally.
1. Map an LDAP or OIDC group to include SuperAdmin using v2 group mapping APIs or the UI.
1. A user authenticates through LDAP or SSO and receives SuperAdmin privileges through a group-derived role binding.
1. The SuperAdmin user can manage global runtime configuration and group operations as allowed by RBAC.

## Break-glass recovery

If all local YugabyteDB Anywhere users are removed and LDAP is unavailable, YugabyteDB Anywhere can become inaccessible. In this case, recover access by injecting a local SuperAdmin directly into the YugabyteDB Anywhere Postgres database using `add_superadmin_user.py` through `py_wrapper.sh`.

### Recovery steps

1. Log in to the YugabyteDB Anywhere host and go to `yb_devops_home`.
1. Run the script using `py_wrapper.sh` with email, password, and install type.
1. Sign in to YugabyteDB Anywhere with the newly created local SuperAdmin user.

### Command templates

Standalone Postgres:

```sh
./bin/py_wrapper.sh ./bin/add_superadmin_user.py \
  --email admin@example.com --password 'Secret123!' -t standalone \
  --application-conf ../../yugaware/conf/application.conf
```

Docker-based Postgres:

```sh
export DOCKER_POSTGRES_CONTAINER=yugaware-postgres
export POSTGRES_USER=postgres
export POSTGRES_DB=yugaware
export POSTGRES_HOST=localhost
export POSTGRES_PORT=5432
./bin/py_wrapper.sh ./bin/add_superadmin_user.py \
  --email admin@example.com --password 'Secret123!' -t docker \
  --application-conf ../../yugaware/conf/application.conf
```

Kubernetes:

```sh
./bin/py_wrapper.sh ./bin/add_superadmin_user.py \
  -t kubernetes -e admin@example.com -p 'Secret123!' \
  -n yb-platform -f /path/to/kubeconfig
```

### Important notes

- Required inputs are `--email`, `--password`, and `--install-type` (`-t`).
- Use `--application-conf ../../yugaware/conf/application.conf` when the default path does not apply.
- Use `--customer-uuid` when customer auto-resolution is ambiguous (for example, in multi-customer setups).
- Run `./bin/py_wrapper.sh ./bin/add_superadmin_user.py --help` for full option details.

## Best practices

- Keep `yb.security.allow_superadmin_user_group_mapping` disabled by default; enable it only when needed.
- Use LDAP/OIDC group mappings for controlled SuperAdmin assignment rather than ad hoc broad grants.
- Periodically audit users and groups with SuperAdmin privileges.
- Verify in staging after upgrades that Admin users cannot perform SuperAdmin-only destructive actions, including SuperAdmin user deletion.

Existing RBAC permissions still govern who can perform role, group, and user operations.
