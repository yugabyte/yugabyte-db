// Copyright (c) YugabyteDB, Inc.

import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.verify;

import org.junit.Test;
import org.slf4j.Logger;

public class GlobalExceptionHandlerTest {

  @Test
  public void testUncaughtExceptionLogsError() throws Exception {
    Logger mockLogger = mock(Logger.class);

    // Create an instance of the handler
    GlobalExceptionHandler handler = new GlobalExceptionHandler();
    GlobalExceptionHandler.setLogger(mockLogger);

    Thread thread = new Thread(() -> {});
    Throwable exception = new RuntimeException("Test exception");

    handler.uncaughtException(thread, exception);

    // Verify that the logger was called with the expected message
    verify(mockLogger)
        .error("Yugaware uncaught exception in thread '{}'", thread.getName(), exception);
  }
}
