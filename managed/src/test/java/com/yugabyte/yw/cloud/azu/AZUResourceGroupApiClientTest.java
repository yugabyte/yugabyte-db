package com.yugabyte.yw.cloud.azu;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertSame;
import static org.junit.Assert.assertThrows;

import com.azure.core.management.exception.ManagementError;
import com.azure.core.management.exception.ManagementException;
import java.util.concurrent.atomic.AtomicInteger;
import org.junit.Test;

public class AZUResourceGroupApiClientTest {

  private static ManagementException azureError(String code) {
    return new ManagementException(code, null, new ManagementError(code, code));
  }

  @Test
  public void testRetryOnRetryableError() {
    AtomicInteger calls = new AtomicInteger();
    String result =
        AZUResourceGroupApiClient.retryOnRetryableError(
            () -> {
              if (calls.incrementAndGet() == 1) {
                throw azureError("ConflictingConcurrentWriteNotAllowed");
              }
              return "done";
            },
            0);
    assertEquals("done", result);
    assertEquals(2, calls.get());

    calls.set(0);
    ManagementException other = azureError("InvalidResourceReference");
    ManagementException thrown =
        assertThrows(
            ManagementException.class,
            () ->
                AZUResourceGroupApiClient.retryOnRetryableError(
                    () -> {
                      calls.incrementAndGet();
                      throw other;
                    },
                    0));
    assertSame(other, thrown);
    assertEquals(1, calls.get());
  }
}
