package com.yugabyte.yw.common.utils;

import java.util.Arrays;
import java.util.stream.Stream;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class Version implements Comparable<Version> {

  private final int versionParts[];
  private final Compatible compatibility;

  public Version(String version) {

    String originalVersion = version;
    if (version == null || version.trim().isEmpty())
      throw new IllegalArgumentException("version cannot be empty");

    if (version.contains(String.valueOf(Compatible.UPWARDS.sign)))
      this.compatibility = Compatible.UPWARDS;
    else this.compatibility = Compatible.EXACT;

    String invalidCharsRegex = String.format("[^\\d.%c]", this.compatibility.sign);

    // normalize the string
    version = version.replaceAll(invalidCharsRegex, ".").replaceAll("[.]+", ".");

    // if it is an exact version, sign will not be present (assumed beyond
    // the string)
    int signIndex =
        compatibility == Compatible.EXACT ? version.length() : version.indexOf(compatibility.sign);

    version = version.substring(0, signIndex);

    this.versionParts =
        Stream.of(version.split("\\.")).mapToInt(Integer::parseInt).filter(i -> i >= 0).toArray();
    log.debug(
        "{} is stored as {}{}", originalVersion, Arrays.toString(versionParts), compatibility.sign);
  }

  @Override
  public int compareTo(Version o) {
    if (Version.isEmpty(this) && !Version.isEmpty(o)) return -1;
    if (!Version.isEmpty(this) && Version.isEmpty(o)) return 1;
    if (Version.isEmpty(this) && Version.isEmpty(o)) return Integer.MIN_VALUE;
    log.debug(
        "working with {}, {}", Arrays.toString(versionParts), Arrays.toString(o.versionParts));

    Boolean isSmall = null;
    int i = 0;
    int lastPart = Math.min(versionParts.length, o.versionParts.length);

    // leading parts decide which is greater. The last part has a different
    // semantics
    for (; i < lastPart - 1; i++) {

      // like 2.15.0 vs 3.1.0
      if (versionParts[i] < o.versionParts[i]) {
        isSmall = true;
        break;
      } else if (versionParts[i] > o.versionParts[i]) {
        isSmall = false;
        break;
      }
      // continue, if the parts are same
    }

    if (isSmall != null) {
      if (isSmall) {
        return this.compatibility == Compatible.UPWARDS ? 0 : -1;
      } else {
        return -1; // this is not completely true, but that's how we can have it
      }
    }

    // all parts are before the last part are same like 2.15.0 vs 2.15.2
    // like 2.15.2+ vs 2.15.4
    if (versionParts[i] < o.versionParts[i]) {
      if (compatibility == Compatible.UPWARDS) return 0;
      else return -1;
    }

    // like 2.15.2 vs 2.15.1+
    if (versionParts[i] > o.versionParts[i]) {
      if (o.compatibility == Compatible.UPWARDS) return 0;
      else return 1;
    }

    // now the lastPart is also same
    // one object might have more parts like 2.15+ vs 2.15.1
    if (versionParts.length < o.versionParts.length) {
      if (compatibility == Compatible.UPWARDS) return 0;
      else return -1;
    } else
    // like 2.15.1 vs 2.15+
    if (versionParts.length > o.versionParts.length) {
      if (o.compatibility == Compatible.UPWARDS) return 0;
      else return 1;
    }
    // this means both have same parts with same values irrespective of
    // compatibility
    return 0;
  }

  private static boolean isEmpty(Version v) {
    return v == null || v.versionParts == null || v.versionParts.length == 0;
  }

  private enum Compatible {
    UPWARDS('+'),
    EXACT(Character.MIN_VALUE);

    private char sign;

    Compatible(char sign) {
      this.sign = sign;
    }
  };

  @Override
  public String toString() {
    String[] parts =
        Arrays.stream(this.versionParts).mapToObj(String::valueOf).toArray(String[]::new);
    return String.format("%s%c", String.join(".", parts), this.compatibility.sign);
  }
}
