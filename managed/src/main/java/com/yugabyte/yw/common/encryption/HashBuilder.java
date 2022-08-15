package com.yugabyte.yw.common.encryption;

/**
 * Interface to generate hashcodes for passwords and check them in turn
 *
 * @author msoundar
 */
public interface HashBuilder {

  /**
   * returns the hashcode for a given password
   *
   * @param password
   * @return
   */
  String hash(String password);

  /**
   * returns whether the given password lands on to the given hash
   *
   * @param password
   * @param hash
   * @return
   */
  boolean isValid(String password, String hash);
}
