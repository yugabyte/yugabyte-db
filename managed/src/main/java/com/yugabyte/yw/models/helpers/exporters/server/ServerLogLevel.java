package com.yugabyte.yw.models.helpers.exporters.server;

/**
 * yb-master / yb-tserver glog severity levels, ordered ascending by severity so ordinal comparison
 * identifies lines below a configured minimum. Each level maps to the single-character prefix glog
 * emits at the start of a log line (e.g. "I0701 ...").
 */
public enum ServerLogLevel {
  INFO("I"),
  WARNING("W"),
  ERROR("E"),
  FATAL("F");

  private final String glogChar;

  ServerLogLevel(String glogChar) {
    this.glogChar = glogChar;
  }

  public String getGlogChar() {
    return glogChar;
  }
}
