package com.yugabyte.yw.controllers.handlers;

import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.google.inject.Inject;
import com.typesafe.config.Config;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.ShellProcessContext;
import com.yugabyte.yw.common.ShellProcessHandler;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.models.Universe;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.time.format.DateTimeFormatter;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.UUID;
import java.util.stream.Collectors;
import javax.annotation.Nullable;
import javax.inject.Singleton;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

@Singleton
public class SessionHandler {

  public static final Logger LOG = LoggerFactory.getLogger(SessionHandler.class);
  public static final String FILTERED_LOGS_SCRIPT = "bin/filtered_logs.sh";
  public static final DateTimeFormatter DATE_FORMAT =
      DateTimeFormatter.ofPattern("yyyy-MM-dd'T'HH:mm:ss");

  @Inject private Config config;

  @Inject private ShellProcessHandler shellProcessHandler;

  @Inject private RuntimeConfGetter confGetter;

  public Path getFilteredLogs(
      Integer maxLines,
      Universe universe,
      String queryRegex,
      String startDateStr,
      String endDateStr)
      throws PlatformServiceException {
    String logDir = config.getString("log.override.path");
    Path logPath = Paths.get(logDir);

    List<String> regexBuilder = new ArrayList<>();
    if (universe != null) {
      regexBuilder.add(universe.getUniverseUUID().toString());
    }

    if (queryRegex != null) {
      regexBuilder.add(queryRegex);
    }

    String tmpDirectory = confGetter.getGlobalConf(GlobalConfKeys.ybTmpDirectoryPath);
    String grepRegex = buildRegexString(regexBuilder);
    String saveFileStr = tmpDirectory + "/" + UUID.randomUUID().toString() + "-logs";
    ShellResponse response =
        execCommand(
            logPath.toAbsolutePath().toString(),
            saveFileStr,
            grepRegex,
            maxLines,
            startDateStr,
            endDateStr);
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
      String logDir,
      String savePath,
      String grepRegex,
      Integer maxLines,
      @Nullable String startDateStr,
      @Nullable String endDateStr) {
    List<String> commandArgs = new ArrayList<>();
    commandArgs.add(FILTERED_LOGS_SCRIPT);
    commandArgs.add(logDir);
    commandArgs.add(savePath);
    commandArgs.add(grepRegex);
    commandArgs.add(maxLines.toString());
    if (startDateStr != null) {
      commandArgs.add(startDateStr);
    }
    if (endDateStr != null) {
      commandArgs.add(endDateStr);
    }
    String description = String.join(" ", commandArgs);
    return shellProcessHandler.run(
        commandArgs,
        ShellProcessContext.builder()
            .logCmdOutput(true)
            .description(description)
            .timeoutSecs(config.getInt("yb.logging.search_timeout_secs"))
            .build());
  }
}
