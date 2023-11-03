package com.yugabyte.yw.controllers;

import com.cronutils.model.Cron;
import com.cronutils.model.CronType;
import com.cronutils.model.definition.CronDefinition;
import com.cronutils.model.definition.CronDefinitionBuilder;
import com.cronutils.parser.CronParser;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.JsonNodeType;
import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.tasks.subtasks.RunExternalScript;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.common.config.impl.RuntimeConfig;
import com.yugabyte.yw.common.config.impl.SettableRuntimeConfigFactory;
import com.yugabyte.yw.common.rbac.PermissionInfo.Action;
import com.yugabyte.yw.common.rbac.PermissionInfo.ResourceType;
import com.yugabyte.yw.forms.PlatformResults;
import com.yugabyte.yw.models.Audit;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Schedule;
import com.yugabyte.yw.models.Schedule.State;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.extended.UserWithFeatures;
import com.yugabyte.yw.models.helpers.ExternalScriptHelper;
import com.yugabyte.yw.models.helpers.ExternalScriptHelper.ExternalScriptConfObject;
import com.yugabyte.yw.models.helpers.TaskType;
import com.yugabyte.yw.rbac.annotations.AuthzPath;
import com.yugabyte.yw.rbac.annotations.PermissionAttribute;
import com.yugabyte.yw.rbac.annotations.RequiredPermissionOnResource;
import com.yugabyte.yw.rbac.annotations.Resource;
import com.yugabyte.yw.rbac.enums.SourceType;
import io.swagger.annotations.Api;
import java.io.IOException;
import java.nio.file.Files;
import java.util.Objects;
import java.util.UUID;
import org.apache.commons.lang3.math.NumberUtils;
import play.libs.Files.TemporaryFile;
import play.mvc.Http;
import play.mvc.Http.MultipartFormData;
import play.mvc.Result;

@Api(value = "SUPPORT_USE_ONLY", hidden = true)
public class ScheduleScriptController extends AuthenticatedController {

  @Inject SettableRuntimeConfigFactory sConfigFactory;
  @Inject RuntimeConfigFactory runtimeConfigFactory;

  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.UNIVERSE, action = Action.UPDATE),
        resourceLocation = @Resource(path = Util.UNIVERSES, sourceType = SourceType.ENDPOINT))
  })
  public Result externalScriptSchedule(UUID customerUUID, UUID universeUUID, Http.Request request) {
    // Validate Access
    canAccess();
    // Extract script file, parameters and cronExpression.
    MultipartFormData<TemporaryFile> body = request.body().asMultipartFormData();
    String scriptContent = extractScriptString(body);
    String scriptParam = extractScriptParam(body);
    String cronExpression = extractCronExpression(body);
    long timeLimitMins = extractTimeLimitMins(body);

    // Generate external Script task parameters and assign values.
    RunExternalScript.Params taskParams = new RunExternalScript.Params();
    taskParams.customerUUID = customerUUID;
    taskParams.timeLimitMins = Long.toString(timeLimitMins);
    taskParams.platformUrl = request.host();
    taskParams.universeUUID = universeUUID;
    UserWithFeatures user = RequestContext.get(TokenAuthenticator.USER);
    taskParams.userUUID = user.getUser().getUuid();

    // Using RuntimeConfig to save the script params because this isn't intended to be that commonly
    // used. If we start using it more commonly, we should migrate to a separate db table for these
    // settings.
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getOrBadRequest(universeUUID, customer);
    RuntimeConfig<Universe> config = sConfigFactory.forUniverse(universe);

    // Check if a script is already scheduled for this universe.
    if (config.hasPath(ExternalScriptHelper.EXT_SCRIPT_SCHEDULE_CONF_PATH)) {
      Schedule schedule =
          Schedule.getOrBadRequest(
              UUID.fromString(
                  config.getString(ExternalScriptHelper.EXT_SCRIPT_SCHEDULE_CONF_PATH)));
      if (!schedule.getStatus().equals(State.Stopped)) {
        throw new PlatformServiceException(
            BAD_REQUEST, "A External Script is already scheduled for this universe.");
      }
    }

    // Create a scheduler for the script.
    Schedule schedule =
        Schedule.create(customerUUID, taskParams, TaskType.ExternalScript, 0L, cronExpression);

    final ObjectMapper mapper = new ObjectMapper();
    try {
      ExternalScriptConfObject runtimeConfigObject =
          new ExternalScriptConfObject(
              scriptContent, scriptParam, schedule.getScheduleUUID().toString());
      String json = mapper.writeValueAsString(runtimeConfigObject);
      config.setValue(ExternalScriptHelper.EXT_SCRIPT_RUNTIME_CONFIG_PATH, json);
    } catch (Exception e) {
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR, "Runtime config for script errored out with: " + e.getMessage());
    }

    auditService()
        .createAuditEntry(
            request,
            Audit.TargetType.ScheduledScript,
            Objects.toString(schedule.getScheduleUUID(), null),
            Audit.ActionType.ExternalScriptSchedule);
    return PlatformResults.withData(schedule);
  }

  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.UNIVERSE, action = Action.UPDATE),
        resourceLocation = @Resource(path = Util.UNIVERSES, sourceType = SourceType.ENDPOINT))
  })
  public Result stopScheduledScript(UUID customerUUID, UUID universeUUID, Http.Request request) {
    // Validate Access
    canAccess();
    // Validate Customer
    Customer customer = Customer.getOrBadRequest(customerUUID);

    // Extract scheduleUUID from RuntimeConfig DB inserted at the time of scheduling
    // script.
    Universe universe = Universe.getOrBadRequest(universeUUID, customer);
    RuntimeConfig<Universe> config = sConfigFactory.forUniverse(universe);
    Schedule schedule;
    try {
      UUID scheduleUUID =
          UUID.fromString(config.getString(ExternalScriptHelper.EXT_SCRIPT_SCHEDULE_CONF_PATH));
      schedule = Schedule.getOrBadRequest(scheduleUUID);
    } catch (Exception e) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "No script is scheduled for this universe. it was trying to search at "
              + ExternalScriptHelper.EXT_SCRIPT_SCHEDULE_CONF_PATH);
    }

    if (schedule.getStatus().equals(State.Stopped)) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Script is already stopped for this universe.");
    }
    schedule.stopSchedule();

    auditService()
        .createAuditEntry(
            request,
            Audit.TargetType.ScheduledScript,
            Objects.toString(schedule.getScheduleUUID(), null),
            Audit.ActionType.StopScheduledScript);
    return PlatformResults.withData(schedule);
  }

  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.UNIVERSE, action = Action.UPDATE),
        resourceLocation = @Resource(path = Util.UNIVERSES, sourceType = SourceType.ENDPOINT))
  })
  public Result updateScheduledScript(UUID customerUUID, UUID universeUUID, Http.Request request) {
    // Validate Access
    canAccess();
    // Extract script file, parameters and cronExpression.
    MultipartFormData<TemporaryFile> body = request.body().asMultipartFormData();
    String scriptContent = extractScriptString(body);
    String scriptParam = extractScriptParam(body);
    String cronExpression = extractCronExpression(body);
    long timeLimitMins = extractTimeLimitMins(body);

    // Generate task params.
    RunExternalScript.Params taskParams = new RunExternalScript.Params();
    taskParams.customerUUID = customerUUID;
    taskParams.timeLimitMins = Long.toString(timeLimitMins);
    taskParams.platformUrl = request.host();
    taskParams.universeUUID = universeUUID;
    UserWithFeatures user = RequestContext.get(TokenAuthenticator.USER);
    taskParams.userUUID = user.getUser().getUuid();

    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getOrBadRequest(universeUUID, customer);
    RuntimeConfig<Universe> config = sConfigFactory.forUniverse(universe);

    // Extract the already present External Script Scheduler for universe.
    Schedule schedule;
    try {
      UUID scheduleUUID =
          UUID.fromString(config.getString(ExternalScriptHelper.EXT_SCRIPT_SCHEDULE_CONF_PATH));
      schedule = Schedule.getOrBadRequest(scheduleUUID);
      if (schedule.getStatus().equals(State.Stopped)) {
        throw new PlatformServiceException(
            BAD_REQUEST, "No running script found for this universe.");
      }
    } catch (Exception e) {
      throw new PlatformServiceException(BAD_REQUEST, "No script is scheduled for this universe.");
    }

    // updating existing schedule task params and cronExpression.
    schedule.setCronExpressionAndTaskParams(cronExpression, taskParams);
    schedule.save();

    final ObjectMapper mapper = new ObjectMapper();
    try {
      ExternalScriptConfObject runtimeConfigObject =
          new ExternalScriptConfObject(
              scriptContent, scriptParam, schedule.getScheduleUUID().toString());
      String json = mapper.writeValueAsString(runtimeConfigObject);
      config.setValue(ExternalScriptHelper.EXT_SCRIPT_RUNTIME_CONFIG_PATH, json);
    } catch (Exception e) {
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR, "Runtime config for script errored out with: " + e.getMessage());
    }
    auditService()
        .createAuditEntry(
            request,
            Audit.TargetType.ScheduledScript,
            Objects.toString(schedule.getScheduleUUID(), null),
            Audit.ActionType.UpdateScheduledScript);
    return PlatformResults.withData(schedule);
  }

  private String extractScriptString(MultipartFormData<TemporaryFile> body) {
    MultipartFormData.FilePart<TemporaryFile> file = body.getFile("script");
    if (file == null || file.getFilename().length() == 0) {
      throw new PlatformServiceException(BAD_REQUEST, "Script file not found");
    }
    try {
      return new String(Files.readAllBytes(file.getRef().path()));
    } catch (IOException e) {
      throw new PlatformServiceException(BAD_REQUEST, e.getMessage());
    }
  }

  private String extractScriptParam(MultipartFormData<TemporaryFile> body) {
    if (body.asFormUrlEncoded().get("scriptParameter") != null) {
      try {
        String scriptParams = body.asFormUrlEncoded().get("scriptParameter")[0];
        // Validate script parameter json format.
        final ObjectMapper mapper = new ObjectMapper();
        JsonNode jsonNode = mapper.readTree(scriptParams);
        JsonNodeType jsonNodeType = jsonNode.getNodeType();
        if (jsonNodeType != JsonNodeType.OBJECT) {
          throw new Exception(
              "Given Json is: "
                  + jsonNodeType.toString()
                  + "type, please provide a json of object type");
        }
        return scriptParams;
      } catch (Exception e) {
        throw new PlatformServiceException(BAD_REQUEST, e.getMessage());
      }
    } else {
      return null;
    }
  }

  private String extractCronExpression(MultipartFormData<TemporaryFile> body) {
    String cronExpression;
    if (body.asFormUrlEncoded().get("cronExpression") != null) {
      // Validate cronExpression if present
      try {
        cronExpression = body.asFormUrlEncoded().get("cronExpression")[0];
        CronDefinition cronDefinition = CronDefinitionBuilder.instanceDefinitionFor(CronType.UNIX);
        CronParser parser = new CronParser(cronDefinition);
        Cron quartzCron = parser.parse(cronExpression);
        quartzCron.validate();
      } catch (Exception e) {
        throw new PlatformServiceException(BAD_REQUEST, "Please provide a valid cronExpression");
      }
    } else {
      throw new PlatformServiceException(BAD_REQUEST, "No cronExpression found");
    }
    return cronExpression;
  }

  private long extractTimeLimitMins(MultipartFormData<TemporaryFile> body) {
    String[] timeLimitMinsParams =
        body.asFormUrlEncoded().getOrDefault("timeLimitMins", new String[] {"0"});
    long timeLimitMins = NumberUtils.toLong(timeLimitMinsParams[0], 0L);
    if (timeLimitMins == 0L) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Please provide valid timeLimitMins for script execution.");
    }
    return timeLimitMins;
  }

  private void canAccess() {
    if (!runtimeConfigFactory
        .staticApplicationConf()
        .getBoolean(ExternalScriptHelper.EXT_SCRIPT_ACCESS_FULL_PATH)) {
      throw new PlatformServiceException(
          BAD_REQUEST, "External Script APIs are disabled. Please contact support team");
    }
  }
}
