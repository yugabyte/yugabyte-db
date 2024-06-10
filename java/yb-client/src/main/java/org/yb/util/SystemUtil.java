package org.yb.util;

import java.util.concurrent.Executor;
import java.util.concurrent.ExecutorService;

public class SystemUtil {
  public static final boolean IS_LINUX =
    System.getProperty("os.name").equalsIgnoreCase("linux");

  /**
   * Netty 3 had logic to shutdown executors in releaseExternalResources call.
   * In Netty 4 we don't have this logic - so need to close executors ourselves.
   * Copied over this method from netty 3 as is to make behavior consistent.
   * **/
  public static void forceShutdownExecutor(Executor executor) {
    if (executor instanceof ExecutorService) {
      ExecutorService es = (ExecutorService)executor;

      try {
        es.shutdownNow();
      } catch (SecurityException ignore) {
        try {
          es.shutdown();
        } catch (SecurityException | NullPointerException alsoIgnore) {
        }
      } catch (NullPointerException ignore) {
      }
    }
  }
}
