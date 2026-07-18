// Copyright (c) YugabyteDB, Inc.

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

public class GlobalExceptionHandler implements Thread.UncaughtExceptionHandler {

  private static Logger logger = LoggerFactory.getLogger(GlobalExceptionHandler.class);

  // Added for test
  public static void setLogger(Logger l) {
    logger = l;
  }

  @Override
  public void uncaughtException(Thread t, Throwable e) {
    // Log the exception
    logger.error("Yugaware uncaught exception in thread '{}'", t.getName(), e);
  }
}
