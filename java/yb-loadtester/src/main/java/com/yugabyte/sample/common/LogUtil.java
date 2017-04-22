package com.yugabyte.sample.common;

import org.apache.log4j.ConsoleAppender;
import org.apache.log4j.Level;
import org.apache.log4j.Logger;
import org.apache.log4j.PatternLayout;

public class LogUtil {
  public static void configureLogLevel(boolean verbose) {
    // First remove all appenders.
    Logger.getLogger("com.yugabyte.sample").removeAppender("YBConsoleLogger");
    Logger.getRootLogger().removeAppender("YBConsoleLogger");;

    // Create the console appender.
    ConsoleAppender console = new ConsoleAppender();
    console.setName("YBConsoleLogger");
    String PATTERN = "%d [%p|%c|%C{1}] %m%n";
    console.setLayout(new PatternLayout(PATTERN));
    console.setThreshold(verbose ? Level.DEBUG : Level.INFO);
    console.activateOptions();

    // Set the desired logging level.
    if (verbose) {
      // If verbose, make everything DEBUG log level and output to console.
      Logger.getRootLogger().addAppender(console);
      Logger.getRootLogger().setLevel(Level.DEBUG);
    } else {
      // If not verbose, only YB sample app INFO logs go to console.
      Logger.getLogger("com.yugabyte.sample").addAppender(console);
      Logger.getRootLogger().setLevel(Level.ERROR);
    }
  }
}
