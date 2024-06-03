/** */
package com.yugabyte.yw.common.encryption.bc;

import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;

import org.junit.BeforeClass;
import org.junit.Test;
import org.mindrot.jbcrypt.BCrypt;

/**
 * @author msoundar
 */
public class BcOpenBsdHasherTest {

  private final BcOpenBsdHasher hasher = new BcOpenBsdHasher();
  private final String password = "somePassword";

  /**
   * @throws java.lang.Exception
   */
  @BeforeClass
  public static void setUpBeforeClass() throws Exception {}

  @Test
  public void testHashing() {
    String hash = hasher.hash(password);
    boolean isValid = hasher.isValid(password, hash);
    assertTrue(isValid);
  }

  @Test
  public void testNullPassword() {
    assertThrows(IllegalArgumentException.class, () -> hasher.hash(null));
  }

  @Test
  public void testInvalidPassword() {
    String hash = hasher.hash(password);
    boolean isValid = hasher.isValid("invalidPassword", hash);
    assertFalse(isValid);
  }

  @Test
  public void testMindrotBcryptCompatibility() {
    String hash = BCrypt.hashpw(password, BCrypt.gensalt());
    boolean isValid = hasher.isValid(password, hash);
    assertTrue(isValid);
  }
}
