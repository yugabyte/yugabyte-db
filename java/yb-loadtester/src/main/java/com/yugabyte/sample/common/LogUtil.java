//
// Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//
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
      // If not verbose, allow YB sample app and driver INFO logs go to console.
      Logger.getLogger("com.yugabyte.sample").addAppender(console);
      Logger.getLogger("com.yugabyte.driver").addAppender(console);
      Logger.getLogger("com.datastax.driver").addAppender(console);
      Logger.getRootLogger().setLevel(Level.WARN);
    }
  }
}
