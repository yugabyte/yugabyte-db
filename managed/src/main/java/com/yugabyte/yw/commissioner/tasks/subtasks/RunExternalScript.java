package com.yugabyte.yw.commissioner.tasks.subtasks;

import static com.yugabyte.yw.controllers.ScheduleScriptController.PLT_EXT_SCRIPT_CONTENT;
import static com.yugabyte.yw.controllers.ScheduleScriptController.PLT_EXT_SCRIPT_PARAM;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.common.ShellProcessHandler;
import com.yugabyte.yw.common.Util;
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
import java.util.Arrays;
import java.util.HashMap;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;

@Slf4j
public class RunExternalScript extends AbstractTaskBase {
  @Inject private ShellProcessHandler shellProcessHandler;

  @Inject
  protected RunExternalScript(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Inject private SettableRuntimeConfigFactory sConfigFactory;

  @Inject private play.Configuration appConfig;

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

      List<String> keys = Arrays.asList(PLT_EXT_SCRIPT_CONTENT, PLT_EXT_SCRIPT_PARAM);
      Map<String, String> configKeysMap;
      try {
        // Extracting the set of keys in synchronized way as they are interconnected and During the
        // scheduled script update the task should not extract partially updated multi keys.
        configKeysMap = Util.getLockedMultiKeyConfig(config, keys);
      } catch (Exception e) {
        throw new RuntimeException(
            "Extrenal Script Task failed as the schedule is stopped and this is a old task");
      }

      // Create a temporary file to store script and make it executable.
      String devopsHome = appConfig.getString("yb.devops.home");
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
        output.write(configKeysMap.get(PLT_EXT_SCRIPT_CONTENT));
      }
      if (!tempScriptFile.setExecutable(true)) {
        throw new RuntimeException("script file permission change failed " + tempScriptFile);
      }

      // Add the commands to the script.
      List<String> commandList = new ArrayList<>();
      commandList.add(SCRIPT_DIR + tempScriptFile.getName());
      commandList.add("--universe_name");
      commandList.add(universe.name);
      commandList.add("--universe_uuid");
      commandList.add(params().universeUUID.toString());
      commandList.add("--platform_url");
      commandList.add(params().platformUrl);
      commandList.add("--auth_token");
      Users.getOrBadRequest(params().userUUID);
      commandList.add(Users.getOrBadRequest(params().userUUID).createAuthToken());

      String scriptParam = configKeysMap.get(PLT_EXT_SCRIPT_PARAM);
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
        if (tempScriptFile.delete()) {
          log.warn("Failed to delete file {}", tempScriptFile);
        }
      }
    }
    log.info("Finished {} task.", getName());
  }
}
