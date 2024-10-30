// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import static com.yugabyte.yw.models.helpers.ExternalScriptHelper.EXT_SCRIPT_CONTENT_CONF_PATH;
import static com.yugabyte.yw.models.helpers.ExternalScriptHelper.EXT_SCRIPT_PARAMS_CONF_PATH;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.google.common.annotations.VisibleForTesting;
import com.google.inject.Inject;
import com.typesafe.config.ConfigException;
import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.common.ShellProcessHandler;
import com.yugabyte.yw.common.config.impl.RuntimeConfig;
import com.yugabyte.yw.common.config.impl.SettableRuntimeConfigFactory;
import com.yugabyte.yw.forms.AbstractTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Users;
import java.io.File;
import java.io.FileOutputStream;
import java.io.OutputStreamWriter;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;

@Slf4j
public class RunExternalScript extends AbstractTaskBase {

  @VisibleForTesting
  static final String STALE_TASK_RAN_ERR =
      "External Script Task failed as the schedule is stopped and this is an old task";

  @Inject private ShellProcessHandler shellProcessHandler;

  @Inject
  protected RunExternalScript(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Inject private SettableRuntimeConfigFactory sConfigFactory;

  private static final String TEMP_SCRIPT_FILE_NAME = "tempScript_";
  private static final String SCRIPT_DIR = "tmp_external_scripts/";
  private static final String SCRIPT_STORE_DIR = "/tmp_external_scripts";

  public static class Params extends AbstractTaskParams {
    public String platformUrl;
    public String timeLimitMins;
    public UUID customerUUID;
    public UUID universeUUID;
    public UUID userUUID;
  }

  public Params params() {
    return (Params) taskParams;
  }

  @Override
  public void run() {
    File tempScriptFile = null;
    try {
      Universe universe = Universe.getOrBadRequest(params().universeUUID);
      RuntimeConfig<Universe> config = sConfigFactory.forUniverse(universe);
      String content, params;
      try {
        content = config.getString(EXT_SCRIPT_CONTENT_CONF_PATH);
        params = config.getString(EXT_SCRIPT_PARAMS_CONF_PATH);
      } catch (ConfigException.Missing e) {
        throw new IllegalStateException(STALE_TASK_RAN_ERR);
      }

      // Create a temporary file to store script and make it executable.
      String devopsHome = sConfigFactory.globalRuntimeConf().getString("yb.devops.home");
      File directory = new File(devopsHome + SCRIPT_STORE_DIR);
      if (!directory.exists()) {
        if (!directory.mkdir()) {
          throw new RuntimeException("Failed to mkdir " + directory);
        }
      }
      tempScriptFile =
          File.createTempFile(
              TEMP_SCRIPT_FILE_NAME + params().universeUUID.toString(), ".py", directory);

      FileOutputStream file = new FileOutputStream(tempScriptFile.getAbsoluteFile());
      try (OutputStreamWriter output = new OutputStreamWriter(file, StandardCharsets.UTF_8)) {
        output.write(content);
      }
      if (!tempScriptFile.setExecutable(true)) {
        throw new RuntimeException("script file permission change failed " + tempScriptFile);
      }

      // Add the commands to the script.
      List<String> commandList = new ArrayList<>();
      commandList.add(SCRIPT_DIR + tempScriptFile.getName());
      commandList.add("--universe_name");
      commandList.add(universe.getName());
      commandList.add("--universe_uuid");
      commandList.add(params().universeUUID.toString());
      commandList.add("--platform_url");
      commandList.add(params().platformUrl);
      commandList.add("--auth_token");
      Users.getOrBadRequest(params().userUUID);
      commandList.add(Users.getOrBadRequest(params().userUUID).createAuthToken());

      String scriptParam = params;
      if (!StringUtils.isEmpty(scriptParam)) {
        final ObjectMapper mapper = new ObjectMapper();
        JsonNode jsonNode = mapper.readTree(scriptParam);
        Iterator<Map.Entry<String, JsonNode>> fields = jsonNode.fields();
        while (fields.hasNext()) {
          Map.Entry<String, JsonNode> field = fields.next();
          String fieldName = field.getKey();
          String fieldValue = field.getValue().asText();
          if (!StringUtils.isEmpty(fieldName) && !StringUtils.isEmpty(fieldValue)) {
            commandList.add("--" + fieldName);
            commandList.add(fieldValue);
          }
        }
      }

      String description = String.join(" ", commandList);

      // Execute the command.
      shellProcessHandler.run(commandList, new HashMap<>(), description).processErrors();
    } catch (Exception e) {
      log.error("Error executing task {}, error='{}'", getName(), e.getMessage(), e);
      throw new RuntimeException(e);
    } finally {
      // Delete temporary file if exists.
      if (tempScriptFile != null && tempScriptFile.exists()) {
        if (!tempScriptFile.delete()) {
          log.warn("Failed to delete file {}", tempScriptFile);
        }
      }
    }
    log.info("Finished {} task.", getName());
  }
}
