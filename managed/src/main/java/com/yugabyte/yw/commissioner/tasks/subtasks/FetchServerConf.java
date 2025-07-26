// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import static play.mvc.Http.Status.NOT_FOUND;

import com.fasterxml.jackson.annotation.JsonIgnore;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.models.NodeAgent;
import java.io.BufferedReader;
import java.io.StringReader;
import java.util.Map;
import java.util.Properties;
import java.util.function.Consumer;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import javax.inject.Inject;
import lombok.ToString;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;

@Slf4j
public class FetchServerConf extends NodeTaskBase {

  @Inject
  protected FetchServerConf(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  public static class Params extends NodeTaskParams {
    public String ip;
    public ServerType serverType;
    public String serviceName;
    public boolean userSystemd;
    @JsonIgnore public Consumer<Output> consumer;
  }

  @ToString
  public static class Output {
    public String ip;
    public ServerType serverType;
    public String binaryPath;
    public String confPath;
    public Map<String, String> gflags;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  private Map<String, String> readGflags(NodeAgent nodeAgent, String confPath) {
    ShellResponse response =
        nodeAgentClient
            .executeCommand(
                nodeAgent, ImmutableList.of("bash", "-c", String.format("cat %s", confPath)))
            .processErrors();
    log.debug("Got server conf file from node {}: {}", taskParams().ip, response.message);
    ImmutableMap.Builder<String, String> mapBuilder = ImmutableMap.builder();
    try (BufferedReader reader = new BufferedReader(new StringReader(response.message))) {
      String line;
      while ((line = reader.readLine()) != null) {
        line = line.trim();
        if (StringUtils.isNotBlank(line)) {
          String[] tokens = line.split("\\s*=\\s*");
          String gflag = StringUtils.stripStart(tokens[0], "-");
          mapBuilder.put(gflag, tokens.length > 1 ? tokens[1] : Boolean.TRUE.toString());
        }
      }
    } catch (Exception e) {
      throw new RuntimeException(e);
    }
    return mapBuilder.build();
  }

  @Override
  public void run() {
    NodeAgent nodeAgent =
        NodeAgent.maybeGetByIp(taskParams().ip)
            .orElseThrow(
                () ->
                    new PlatformServiceException(
                        NOT_FOUND, "Node agent is not found for ip " + taskParams().ip));
    ImmutableList.Builder<String> cmdBuilder = ImmutableList.<String>builder().add("systemctl");
    if (taskParams().userSystemd) {
      cmdBuilder.add("--user");
    }
    cmdBuilder.add("cat").add(taskParams().serviceName);
    ShellResponse response =
        nodeAgentClient.executeCommand(nodeAgent, cmdBuilder.build()).processErrors();
    log.debug("Got systemd unit file from node {} : {}", taskParams().ip, response.message);
    // Validate the systemd unit by matching the first line.
    Matcher matcher = Pattern.compile("^(#\\s+)(.+)\\R").matcher(response.message);
    if (!matcher.find() || matcher.groupCount() < 2) {
      // # /home/yugabyte/.config/systemd/user/yb-tserver.service
      throw new RuntimeException("Invalid systemctl cat command output: " + response.message);
    }
    Output output = new Output();
    output.ip = taskParams().ip;
    output.serverType = taskParams().serverType;
    try {
      Properties properties = new Properties();
      properties.load(new StringReader(response.message));
      String command = properties.getProperty("ExecStart");
      String[] commandParts = command.split("\\s+(--flagfile)\\s+");
      output.binaryPath = commandParts[0];
      output.confPath = commandParts[1];
      output.gflags = readGflags(nodeAgent, output.confPath);
      log.debug("Fetched gflags for {}: {}", taskParams().serverType, output);
      taskParams().consumer.accept(output);
    } catch (Exception e) {
      throw new RuntimeException(e);
    }
  }
}
