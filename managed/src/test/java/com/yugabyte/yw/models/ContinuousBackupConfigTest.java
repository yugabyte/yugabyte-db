// Copyright (c) YugabyteDB, Inc.
package com.yugabyte.yw.models;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertThrows;
import static play.mvc.Http.Status.BAD_REQUEST;

import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.models.helpers.TimeUnit;
import org.junit.Test;

public class ContinuousBackupConfigTest {

  @Test
  public void testValidateRejectsMilliseconds() {
    ContinuousBackupConfig config = new ContinuousBackupConfig();
    // 4 minutes + 1 millisecond.
    // This is within the acceptable range, but the frequency time unit is milliseconds.
    config.setFrequency(240001L);
    config.setFrequencyTimeUnit(TimeUnit.MILLISECONDS);

    PlatformServiceException exception =
        assertThrows(PlatformServiceException.class, config::validate);
    assertEquals(BAD_REQUEST, exception.getHttpStatus());
    assertEquals("Frequency time unit must be seconds or larger", exception.getMessage());
  }

  @Test
  public void testValidateAcceptsSeconds() {
    ContinuousBackupConfig config = new ContinuousBackupConfig();
    config.setFrequency(120L);
    config.setFrequencyTimeUnit(TimeUnit.SECONDS);

    config.validate();
  }
}
