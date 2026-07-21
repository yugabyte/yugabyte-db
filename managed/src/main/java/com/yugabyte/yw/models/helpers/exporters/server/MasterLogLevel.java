package com.yugabyte.yw.models.helpers.exporters.server;

/**
 * yb-master glog severity levels, ordered ascending by severity so ordinal comparison identifies
 * lines below a configured minimum. Each level maps to the single-character prefix glog emits at
 * the start of a log line (e.g. "I0701 ...").
 */
public enum MasterLogLevel {
  INFO("I"),
  WARNING("W"),
  ERROR("E"),
  FATAL("F");

  private final String glogChar;

  MasterLogLevel(String glogChar) {
    this.glogChar = glogChar;
  }

  public String getGlogChar() {
    return glogChar;
  }
}
