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
import com.yugabyte.yw.common.config.impl.RuntimeConfig;
import com.yugabyte.yw.common.config.impl.SettableRuntimeConfigFactory;
import com.yugabyte.yw.forms.PlatformResults;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Schedule;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.extended.UserWithFeatures;
import com.yugabyte.yw.models.helpers.TaskType;
import io.swagger.annotations.Api;
import io.swagger.annotations.Authorization;
import java.io.File;
import java.io.IOException;
import java.nio.file.Files;
import java.util.Arrays;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import org.apache.commons.lang3.math.NumberUtils;
import play.mvc.Http;
import play.mvc.Http.MultipartFormData;
import play.mvc.Result;

@Api(
    value = "ScheduleExternalScript",
    authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH))
public class ScheduleScriptController extends AuthenticatedController {

  @Inject SettableRuntimeConfigFactory sConfigFactory;

  public static final String PLT_EXT_SCRIPT_CONTENT = "platform_ext_script_content";
  public static final String PLT_EXT_SCRIPT_PARAM = "platform_ext_script_params";
  public static final String PLT_EXT_SCRIPT_SCHEDULE = "platform_ext_script_schedule";

  public Result externalScriptSchedule(UUID customerUUID, UUID universeUUID) throws IOException {
    // Extract script file, parameters and cronExpression.
    MultipartFormData<File> body = request().body().asMultipartFormData();
    String scriptContent = extractScriptString(body);
    String scriptParam = extractScriptParam(body);
    String cronExpression = extractCronExpression(body);
    long timeLimitMins = extractTimeLimitMins(body);

    // Generate external Script task parameters and assign values.
    RunExternalScript.Params taskParams = new RunExternalScript.Params();
    taskParams.customerUUID = customerUUID;
    taskParams.timeLimitMins = Long.toString(timeLimitMins);
    taskParams.platformUrl = request().host();
    taskParams.universeUUID = universeUUID;
    UserWithFeatures user = (UserWithFeatures) Http.Context.current().args.get("user");
    taskParams.userUUID = user.getUser().uuid;

    // Using RuntimeConfig to save the script params because this isn't intended to be that commonly
    // used. If we start using it more commonly, we should migrate to a separate db table for these
    // settings.
    Universe universe = Universe.getOrBadRequest(universeUUID);
    RuntimeConfig<Universe> config = sConfigFactory.forUniverse(universe);

    // Check if a script is already scheduled for this universe.
    if (config.hasPath(PLT_EXT_SCRIPT_SCHEDULE)) {
      throw new PlatformServiceException(
          BAD_REQUEST, "A External Script is already scheduled for this universe.");
    }

    // Create a scheduler for the script.
    Schedule schedule =
        Schedule.create(customerUUID, taskParams, TaskType.ExternalScript, 0L, cronExpression);

    // Add task details in RunTimeConfig DB.
    Map<String, String> configKeysMap = new HashMap<>();
    configKeysMap.put(PLT_EXT_SCRIPT_CONTENT, scriptContent);
    configKeysMap.put(PLT_EXT_SCRIPT_PARAM, scriptParam);
    configKeysMap.put(PLT_EXT_SCRIPT_SCHEDULE, schedule.scheduleUUID.toString());
    // Inserting the set of keys in synchronized way as they are interconnected and the task in
    // execution should not pick up partially inserted keys.
    Util.setLockedMultiKeyConfig(config, configKeysMap);

    return PlatformResults.withData(schedule);
  }

  public Result stopScheduledScript(UUID customerUUID, UUID universeUUID) {
    // Validate Customer
    Customer.getOrBadRequest(customerUUID);

    // Extract scheduleUUID from RuntimeConfig DB inserted at the time of scheduling
    // script.
    Universe universe = Universe.getOrBadRequest(universeUUID);
    RuntimeConfig<Universe> config = sConfigFactory.forUniverse(universe);
    UUID scheduleUUID;
    try {
      scheduleUUID = UUID.fromString(config.getString(PLT_EXT_SCRIPT_SCHEDULE));
    } catch (Exception e) {
      throw new PlatformServiceException(BAD_REQUEST, "No script is scheduled for this universe.");
    }
    Schedule schedule = Schedule.getOrBadRequest(scheduleUUID);
    schedule.stopSchedule();

    // Remove the entries of schedule and script from RunTime Config DB.
    List<String> configKeysList =
        Arrays.asList(PLT_EXT_SCRIPT_CONTENT, PLT_EXT_SCRIPT_PARAM, PLT_EXT_SCRIPT_SCHEDULE);
    // Deleting the set of keys in synchronized way as they are interconnected and the task in
    // execution should not call partially deleted set of keys.
    Util.deleteLockedMultiKeyConfig(config, configKeysList);
    return PlatformResults.withData(schedule);
  }

  public Result updateScheduledScript(UUID customerUUID, UUID universeUUID) throws IOException {

    // Extract script file, parameters and cronExpression.
    MultipartFormData<File> body = request().body().asMultipartFormData();
    String scriptContent = extractScriptString(body);
    String scriptParam = extractScriptParam(body);
    String cronExpression = extractCronExpression(body);
    long timeLimitMins = extractTimeLimitMins(body);

    // Generate task params.
    RunExternalScript.Params taskParams = new RunExternalScript.Params();
    taskParams.customerUUID = customerUUID;
    taskParams.timeLimitMins = Long.toString(timeLimitMins);
    taskParams.platformUrl = request().host();
    taskParams.universeUUID = universeUUID;
    UserWithFeatures user = (UserWithFeatures) Http.Context.current().args.get("user");
    taskParams.userUUID = user.getUser().uuid;

    Universe universe = Universe.getOrBadRequest(universeUUID);
    RuntimeConfig<Universe> config = sConfigFactory.forUniverse(universe);

    // Extract the already present External Script Scheduler for universe.
    UUID scheduleUUID;
    try {
      scheduleUUID = UUID.fromString(config.getString(PLT_EXT_SCRIPT_SCHEDULE));
    } catch (Exception e) {
      throw new PlatformServiceException(BAD_REQUEST, "No script is scheduled for this universe.");
    }
    Schedule schedule = Schedule.getOrBadRequest(scheduleUUID);

    Map<String, String> configKeysMap = new HashMap<>();
    configKeysMap.put(PLT_EXT_SCRIPT_CONTENT, scriptContent);
    configKeysMap.put(PLT_EXT_SCRIPT_PARAM, scriptParam);

    // updating exsisting schedule task params and cronExperssion.
    schedule.setCronExperssionandTaskParams(cronExpression, taskParams);
    // Inserting the set of keys in synchronized way as they are interconnected and the task in
    // execution should not extract partially inserted keys.
    Util.setLockedMultiKeyConfig(config, configKeysMap);
    return PlatformResults.withData(schedule);
  }

  private String extractScriptString(MultipartFormData<File> body) throws IOException {
    MultipartFormData.FilePart<File> file = body.getFile("script");
    if (file == null || file.getFilename().length() == 0) {
      throw new PlatformServiceException(BAD_REQUEST, "Script file not found");
    }
    return new String(Files.readAllBytes(file.getFile().toPath()));
  }

  private String extractScriptParam(MultipartFormData<File> body) {
    String scriptParams = "";
    if (body.asFormUrlEncoded().get("scriptParameter") != null) {
      scriptParams = body.asFormUrlEncoded().get("scriptParameter")[0];
    } else {
      return scriptParams;
    }
    try {
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
  }

  private String extractCronExpression(MultipartFormData<File> body) {
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

  private long extractTimeLimitMins(MultipartFormData<File> body) {
    String[] timeLimitMinsParams =
        body.asFormUrlEncoded().getOrDefault("timeLimitMins", new String[] {"0"});
    long timeLimitMins = NumberUtils.toLong(timeLimitMinsParams[0], 0L);
    if (timeLimitMins == 0L) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Please provide valid timeLimitMins for script execution.");
    }
    return timeLimitMins;
  }
}
