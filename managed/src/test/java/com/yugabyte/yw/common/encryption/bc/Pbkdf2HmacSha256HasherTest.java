// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.encryption.bc;

import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;

import java.util.concurrent.atomic.AtomicReference;
import org.bouncycastle.crypto.CryptoServicesRegistrar;
import org.junit.Test;

public class Pbkdf2HmacSha256HasherTest {

  private final Pbkdf2HmacSha256Hasher hasher = new Pbkdf2HmacSha256Hasher();

  @Test
  public void testHashAndValidate() {
    String hash = hasher.hash("a-long-enough-password");
    assertTrue(hasher.isValid("a-long-enough-password", hash));
    assertFalse(hasher.isValid("another-long-password", hash));
  }

  // FIPS approved-only mode refuses PBKDF2 passwords under 112 bits. A short wrong password must
  // be rejected as a mismatch rather than surfacing the module's error (a 500 on login).
  @Test
  public void testShortPasswordInApprovedOnlyModeIsAMismatch() throws Exception {
    String hash = hasher.hash("a-long-enough-password");
    AtomicReference<Throwable> failure = new AtomicReference<>();
    // Approved-only mode is set per thread; a fresh one keeps it away from other tests.
    Thread approvedOnly =
        new Thread(
            () -> {
              try {
                CryptoServicesRegistrar.setApprovedOnlyMode(true);
                assertFalse(hasher.isValid("short", hash));
                assertTrue(hasher.isValid("a-long-enough-password", hash));
              } catch (Throwable t) {
                failure.set(t);
              }
            });
    approvedOnly.start();
    approvedOnly.join();
    assertNull("unexpected failure: " + failure.get(), failure.get());
  }
}
