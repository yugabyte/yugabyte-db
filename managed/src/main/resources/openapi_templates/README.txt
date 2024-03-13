This folder contains mustache template files used for generating the Yugaware
server Play Framework Controllers and models. These are slightly modified from
the original source found here:
https://github.com/OpenAPITools/openapi-generator/tree/v5.0.1/modules/openapi-generator/src/main/resources/JavaPlayFramework.
The changes made to each file is documented in place in the respective files.

The YB vendor extensions are documented here.

1. x-yba-api-visibility and x-yba-api-since
Valid values for x-yba-api-visibility are preview, deprecated and internal (same as the existing
annotation @YbaApi(visibility='xxx')). The x-yba-api-visibility property can be set at various
levels of the openapi spec - entire path (covers routes), entire schema (covers models for request
and response), or properties within schema (covers individual properties of models).

Internal APIs

Whenever the API visibility is marked with x-yba-api-visibility: internal, these parts of the API
are filtered out and only the remaining is written out to src/main/resources/openapi_public.yaml.
This is used as input to generate the public YBA API docs in Stoplight.

Preview APIs

If API is marked with x-yba-api-visibility: preview, it is mandatory to also mark
x-yba-api-since: <yba_version>, for example x-yba-api-since: 2.20.0.0, alongside the visibility.
This is mandated by this script that is run during the sbt build. The script also automatically
generates the corresponding preview warning message to the API description that will show up in
the API docs.

Deprecated APIs

If API is marked with x-yba-api-visibility: deprecated, it is mandatory to also mark
x-yba-api-since: <yba_version>, for example x-yba-api-since: 2.20.0.0, alongside the visibility.
This is mandated by this script that is run during the sbt build. The script also automatically
generates the corresponding deprecation warning message to the API description that will show up
in the API docs.

2. x-yba-api-authz
This property is recognized only on a path element of the openapi spec. The structure of this
object closely follows the @AuthzPath annotation that is applied on v1 controller methods.
The controller class gets generated with the @AuthzPath annotation:

For a complete example:

x-yba-api-authz:
  - requiredPermission:
      resourceType: universe
      action: UPDATE
    resourceLocation:
      path: universes
      sourceType: endpoint
      dbClass: com.yugabyte.yw.models.Backup.class
      identifier: backups
      columnName: backup_uuid
    checkOnlyPermission: true

generates this annotation for the method in the controller class:

@AuthzPath({
  @RequiredPermissionOnResource(
      requiredPermission =
          @PermissionAttribute(resourceType = ResourceType.UNIVERSE, action = Action.UPDATE),
      resourceLocation = @Resource(path = Util.UNIVERSES,
        sourceType = SourceType.ENDPOINT,
        dbClass = com.yugabyte.yw.models.Backup.class,
        identifier = "backups",
        columnName = "backup_uuid"),
      checkOnlyPermission = true)
})

Since this is a mandatory property for a path, if the API does not require Authz permissions, then
specify this:

x-yba-api-authz:
  noAuthz: true

3. x-yba-api-audit
It is mandatory to add this to a path. To explicitly skip audit logging for an API, add this:
x-yba-api-audit:
  noAudit: true

When specified, the corresponding ControllerImpInterface method will have an auditService call to
register a audit entry. The parameters required for this call can be specified as shown below.
Example:
    x-yba-api-audit:
      auditTargetType: Universe
      auditTargetId: uniUUID.toString()
      auditActionType: UpgradeGFlags
      taskUuid: obj.getTaskUuid()
      additionalDetails: true
Description:
    x-yba-api-audit:
      auditTargetType: Should be set to one of com.yugabyte.yw.models.Audit.TargetType enum values.
      auditTargetId: String typed ID of the audit target. Can use contextual variables to set this.
      auditActionType: Should be set to one of com.yugabyte.yw.models.Audit.ActionType enum values.
      taskUuid: String typed ID of the async task. Can use contextual variables to set this.
      additionalDetails: Adding this property generates the GFlags additionalDetails to be logged.
          Can be omitted.