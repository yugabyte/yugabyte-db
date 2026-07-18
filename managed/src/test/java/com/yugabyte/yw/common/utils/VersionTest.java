package com.yugabyte.yw.common.utils;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertThrows;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.junit.runners.BlockJUnit4ClassRunner;

@RunWith(BlockJUnit4ClassRunner.class)
public class VersionTest {

  /** comparing with 2.14+ against single versions */
  @Test
  public void testGroupedCases() {
    Version v2_14p = new Version("2.14+");
    Version v2_14 = new Version("2.14");
    assertEquals(v2_14p.compareTo(v2_14), 0);

    Version v2_14_1 = new Version("2.14.1");
    assertEquals(v2_14p.compareTo(v2_14_1), 0);

    Version v2_15 = new Version("2.15");
    assertEquals(v2_14p.compareTo(v2_15), 0);

    Version v2_15_1 = new Version("2.15.1");
    assertEquals(v2_14p.compareTo(v2_15_1), 0);

    Version v3_0 = new Version("3.0");
    assertEquals(v2_14p.compareTo(v3_0), 0);

    Version v2_13_2 = new Version("2.13.2.0.64");
    assertEquals(v2_14p.compareTo(v2_13_2), 1);

    assertEquals(v2_14.compareTo(v2_14_1), -1);
  }

  /** comparing with 2.14.0-b68+ against single versions */
  @Test
  public void testGroupedCasesInvalidChars() {
    Version v2_14p = new Version("2.14.0-b68+");
    Version v2_14 = new Version("2.14-b89");
    assertEquals(v2_14p.compareTo(v2_14), 0);

    Version v2_14_1 = new Version("2.14.1-b89");
    assertEquals(v2_14p.compareTo(v2_14_1), 0);

    Version v2_15 = new Version("2.15-89");
    assertEquals(v2_14p.compareTo(v2_15), 0);

    Version v2_15_1 = new Version("2.15.1-89");
    assertEquals(v2_14p.compareTo(v2_15_1), 0);

    Version v3_0 = new Version("3.0-b89");
    assertEquals(v2_14p.compareTo(v3_0), 0);
  }

  @Test
  public void testEmptyCases() {
    Version v2_14p = new Version("2.14.0-b68+");
    testNonEmptyWithNull(v2_14p);
    testNonEmptyWithEmptyVersion(v2_14p, "some-version");

    Version v2_14 = new Version("2.14-b89");
    testNonEmptyWithNull(v2_14);
    testNonEmptyWithEmptyVersion(v2_14, "some-version");
  }

  private void testNonEmptyWithEmptyVersion(Version v, String versionStr) {
    assertThrows(
        IllegalArgumentException.class,
        () -> {
          v.compareTo(getEmpty());
        });
    Version empty = new Version(versionStr);
    assertEquals(v.compareTo(empty), 1);
    assertEquals(empty.compareTo(v), -1);
    assertEquals(empty.compareTo(null), Integer.MIN_VALUE);
  }

  private Version getEmpty() {
    return new Version("");
  }

  private void testNonEmptyWithNull(Version v) {
    assertEquals(v.compareTo(null), 1);
  }
}
