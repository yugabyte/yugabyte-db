package com.yugabyte.yw.controllers.handlers;

import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.google.inject.Inject;
import com.typesafe.config.Config;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.ShellProcessHandler;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.models.Universe;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import java.io.File;
import java.io.FileNotFoundException;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.UUID;
import java.util.stream.Collectors;

public class SessionHandler {

  public static final Logger LOG = LoggerFactory.getLogger(SessionHandler.class);
  public static final String FILTERED_LOGS_SCRIPT = "bin/filtered_logs.sh";

  @Inject private Config config;

  @Inject private ShellProcessHandler shellProcessHandler;

  public Path getFilteredLogs(Integer maxLines, Universe universe, String queryRegex)
      throws PlatformServiceException {
    String appHomeDir =
        config.hasPath("application.home") ? config.getString("application.home") : ".";
    String logDir =
        config.hasPath("log.override.path")
            ? config.getString("log.override.path")
            : String.format("%s/logs", appHomeDir);
    Path logPath = Paths.get(logDir);

    List<String> regexBuilder = new ArrayList<>();
    if (universe != null) {
      regexBuilder.add(universe.universeUUID.toString());
    }

    if (queryRegex != null) {
      regexBuilder.add(queryRegex);
    }

    String grepRegex = buildRegexString(regexBuilder);
    String saveFileStr = "/tmp/" + UUID.randomUUID().toString() + "-logs";
    ShellResponse response =
        execCommand(logPath.toAbsolutePath().toString(), saveFileStr, grepRegex, maxLines);
    if (response.code != 0) {
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR, "Could not run filter_logs.sh script.");
    }

    Path filteredLogsPath = Paths.get(saveFileStr);
    return filteredLogsPath;
  }

  private String buildRegexString(List<String> regexBuilder) {
    String regexString = "";
    if (regexBuilder.size() == 0) {
      return regexString;
    }

    List<List<String>> permutedStrings = new ArrayList<>();
    permute(regexBuilder, 0, permutedStrings);

    List<String> regexArr = new ArrayList<>();
    for (List<String> list : permutedStrings) {
      regexArr.add(String.join(".*", list));
    }
    regexArr = regexArr.stream().map(v -> ".*" + v + ".*").collect(Collectors.toList());

    regexString = String.join("|", regexArr);
    return regexString;
  }

  private void permute(List<String> arr, int k, List<List<String>> permutations) {
    for (int i = k; i < arr.size(); i++) {
      Collections.swap(arr, i, k);
      permute(arr, k + 1, permutations);
      Collections.swap(arr, k, i);
    }
    if (k == arr.size() - 1) {
      List<String> copy = new ArrayList<String>(arr);
      permutations.add(copy);
    }
  }

  private ShellResponse execCommand(
      String logDir, String savePath, String grepRegex, Integer maxLines) {
    List<String> commandArgs = new ArrayList<>();
    commandArgs.add(FILTERED_LOGS_SCRIPT);
    commandArgs.add(logDir);
    commandArgs.add(savePath);
    commandArgs.add(grepRegex);
    commandArgs.add(maxLines.toString());
    String description = String.join(" ", commandArgs);
    return shellProcessHandler.run(commandArgs, null, description);
  }
}
