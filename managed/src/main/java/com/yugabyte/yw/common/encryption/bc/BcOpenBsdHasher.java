package com.yugabyte.yw.common.encryption.bc;

import com.yugabyte.yw.common.encryption.HashBuilder;
import java.security.SecureRandom;
import org.springframework.security.crypto.bcrypt.BCrypt;

/**
 * Bouncy Castle's OpenBSD implementation based Hash generator
 *
 * @author msoundar
 */
public class BcOpenBsdHasher implements HashBuilder {

  private static final SecureRandom rnd = new SecureRandom();
  private static final int COST = 12; // arrived after discussions

  @Override
  public String hash(String password) {
    if (password == null) throw new IllegalArgumentException("Password cannot be null");
    String salt = BCrypt.gensalt(COST, rnd);
    return BCrypt.hashpw(password, salt);
  }

  @Override
  public boolean isValid(String password, String hash) {
    if (password == null) throw new IllegalArgumentException("Password cannot be null");
    return BCrypt.checkpw(password, hash);
  }
}
