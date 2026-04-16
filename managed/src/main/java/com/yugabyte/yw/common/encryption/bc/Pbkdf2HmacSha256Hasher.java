package com.yugabyte.yw.common.encryption.bc;

import com.yugabyte.yw.common.encryption.HashBuilder;
import java.security.SecureRandom;
import java.util.Arrays;
import org.apache.commons.lang3.StringUtils;
import org.bouncycastle.crypto.PasswordBasedDeriver;
import org.bouncycastle.crypto.fips.*;
import org.bouncycastle.util.encoders.Base64;

public class Pbkdf2HmacSha256Hasher implements HashBuilder {

  private static final int SALT_SIZE_BYTES = 32;
  private static final int KEY_SIZE_BYTES = 32;

  private static final String SALT_KEY_DELIMITER = ".";
  private static final String SALT_KEY_DELIMITER_REGEX = "\\" + SALT_KEY_DELIMITER;

  private static final SecureRandom rnd = new SecureRandom();

  @Override
  public String hash(String password) {
    if (password == null) throw new IllegalArgumentException("Password cannot be null");
    byte[] salt = new byte[SALT_SIZE_BYTES];
    rnd.nextBytes(salt);
    FipsPBKD.Parameters parameters =
        FipsPBKD.PBKDF2.using(FipsSHS.Algorithm.SHA256_HMAC, password.getBytes()).withSalt(salt);
    byte[] key =
        new FipsPBKD.DeriverFactory()
            .createDeriver(parameters)
            .deriveKey(PasswordBasedDeriver.KeyType.CIPHER, KEY_SIZE_BYTES);
    return "4." + Base64.toBase64String(salt) + SALT_KEY_DELIMITER + Base64.toBase64String(key);
  }

  @Override
  public boolean isValid(String password, String hash) {
    if (password == null) throw new IllegalArgumentException("Password cannot be null");
    String[] versionSaltAndKey = hash.split(SALT_KEY_DELIMITER_REGEX);
    if (versionSaltAndKey.length != 3) {
      // Don't want to write hash value itself into logs.
      throw new IllegalArgumentException("Invalid hash value");
    }
    String version = versionSaltAndKey[0];
    if (!StringUtils.equals(version, "4")) {
      throw new IllegalArgumentException("Only hash version 4 is supported");
    }
    String saltBase64 = versionSaltAndKey[1];
    byte[] salt = Base64.decode(saltBase64);
    String hashKeyBase64 = versionSaltAndKey[2];
    byte[] hashKey = Base64.decode(hashKeyBase64);

    FipsPBKD.Parameters parameters =
        FipsPBKD.PBKDF2.using(FipsSHS.Algorithm.SHA256_HMAC, password.getBytes()).withSalt(salt);
    byte[] key =
        new FipsPBKD.DeriverFactory()
            .createDeriver(parameters)
            .deriveKey(PasswordBasedDeriver.KeyType.CIPHER, hashKey.length);

    return Arrays.equals(hashKey, key);
  }
}
