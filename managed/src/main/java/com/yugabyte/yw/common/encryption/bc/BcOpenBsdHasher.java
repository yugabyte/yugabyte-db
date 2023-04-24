package com.yugabyte.yw.common.encryption.bc;

import com.yugabyte.yw.common.encryption.HashBuilder;
import java.security.SecureRandom;
import org.bouncycastle.crypto.generators.OpenBSDBCrypt;

/**
 * Bouncy Castle's OpenBSD implementation based Hash generator
 *
 * @author msoundar
 */
public class BcOpenBsdHasher implements HashBuilder {

  private static final SecureRandom rnd = new SecureRandom();
  private static final int COST = 12; // arrived after discussions
  private static final byte SALT_LENGTH_BYTES = 16;

  @Override
  public String hash(String password) {
    if (password == null) throw new IllegalArgumentException("Password cannot be null");
    byte[] salt = new byte[SALT_LENGTH_BYTES];
    rnd.nextBytes(salt);
    return OpenBSDBCrypt.generate(password.toCharArray(), salt, COST);
  }

  @Override
  public boolean isValid(String password, String hash) {
    if (password == null) throw new IllegalArgumentException("Password cannot be null");
    return OpenBSDBCrypt.checkPassword(hash, password.toCharArray());
  }
}
