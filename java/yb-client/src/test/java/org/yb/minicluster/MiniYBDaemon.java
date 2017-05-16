/**
 * Copyright (c) YugaByte, Inc.
 */
package org.yb.minicluster;

import com.google.common.net.HostAndPort;
import org.yb.client.TestUtils;

public class MiniYBDaemon {
  private static final String PID_PREFIX = "pid";
  private static final String LOG_PREFIX_SEPARATOR = "|";

  /**
   * @param type daemon type (master / tablet server)
   * @param commandLine command line used to run the daemon
   * @param process daemon process
   */
  public MiniYBDaemon(
      MiniYBDaemonType type, int indexForLog, String[] commandLine, Process process, String bindIp,
      int rpcPort, int webPort, int cqlWebPort, int redisWebPort) {
    this.type = type;
    this.commandLine = commandLine;
    this.process = process;
    this.indexForLog = indexForLog;
    this.bindIp = bindIp;
    this.rpcPort = rpcPort;
    this.webPort = webPort;
    this.cqlWebPort = cqlWebPort;
    this.redisWebPort = redisWebPort;
  }

  public MiniYBDaemonType getType() {
    return type;
  }

  public String[] getCommandLine() {
    return commandLine;
  }

  public Process getProcess() {
    return process;
  }

  public int getPid() throws NoSuchFieldException, IllegalAccessException {
    return TestUtils.pidOfProcess(process);
  }

  String getPidStr() {
    try {
      return String.valueOf(getPid());
    } catch (NoSuchFieldException | IllegalAccessException ex) {
      return "<error_getting_pid>";
    }
  }

  @Override
  public String toString() {
    return type.toString().toLowerCase() + " process on bind IP " + bindIp + ", rpc port " +
        rpcPort + ", web port " + webPort;
  }

  private final MiniYBDaemonType type;
  private final int indexForLog;
  private final String[] commandLine;
  private final Process process;
  private final String bindIp;
  private final int rpcPort;
  private final int webPort;
  private final int cqlWebPort;
  private final int redisWebPort;

  public HostAndPort getWebHostAndPort() {
    return HostAndPort.fromParts(bindIp, webPort);
  }

  /**
   * @return the prefix to be used for each line of this daemon's logs.
   */
  public String getLogPrefix() {
    String withoutHttpPort = type.shortStr() + indexForLog + LOG_PREFIX_SEPARATOR + PID_PREFIX +
        getPidStr() + LOG_PREFIX_SEPARATOR + ":" + rpcPort;
    if (TestUtils.isJenkins()) {
      // No need to provide a clickable web UI link on Jenkins.
      return withoutHttpPort + " ";
    }
    return withoutHttpPort + LOG_PREFIX_SEPARATOR + "http://" + getWebHostAndPort() + " ";
  }

  public int getWebPort() {
    return webPort;
  }

  public int getCqlWebPort() {
    return cqlWebPort;
  }

  // TODO: rename tp getBindIp
  public String getLocalhostIP() {
    return bindIp;
  }

}
