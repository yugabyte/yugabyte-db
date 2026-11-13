// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

/**
 * Splits a CSV value into fields as Commons CSV's DEFAULT format does, keeping where each decoded
 * character came from in the raw value.
 *
 * <p>HbaConfRedactor uses those positions to replace only a password's raw characters, so the rest
 * of the stored value stays exactly as it is. Commons CSV returns decoded values without positions,
 * and printing them back would change how the value is quoted. Its rules are the ones to match
 * because {@code GFlagsUtil.mergeCSVs} reads and re-prints ysql_hba_conf_csv with them.
 */
final class CsvFieldSplitter {

  private CsvFieldSplitter() {}

  /** A CSV field: its decoded text, and where each character starts in the raw value. */
  record CsvField(String text, boolean quoted, int[] rawIndex) {}

  /**
   * Splits a value without line breaks into fields, or returns null where Commons CSV rejects it.
   */
  static List<CsvField> split(String value) {
    List<CsvField> fields = new ArrayList<>();
    int n = value.length();
    int[] raw = new int[n];
    StringBuilder text = new StringBuilder();
    int p = 0;
    while (true) {
      text.setLength(0);
      boolean quoted = p < n && value.charAt(p) == '"';
      int i = quoted ? p + 1 : p;
      while (true) {
        if (i >= n) {
          if (quoted) {
            return null;
          }
          break;
        }
        char c = value.charAt(i);
        if (!quoted && c == ',') {
          break;
        }
        if (quoted && c == '"') {
          if (i + 1 < n && value.charAt(i + 1) == '"') {
            raw[text.length()] = i;
            text.append('"');
            i += 2;
            continue;
          }
          i++;
          while (i < n && value.charAt(i) != ',' && Character.isWhitespace(value.charAt(i))) {
            i++;
          }
          if (i < n && value.charAt(i) != ',') {
            return null;
          }
          break;
        }
        raw[text.length()] = i;
        text.append(c);
        i++;
      }
      fields.add(new CsvField(text.toString(), quoted, Arrays.copyOf(raw, text.length())));
      if (i >= n) {
        return fields;
      }
      p = i + 1;
    }
  }

  /** Where character i of a field ends in the raw value; a quote in a quoted field is doubled. */
  static int rawEnd(CsvField field, int i) {
    return field.rawIndex()[i] + (field.quoted() && field.text().charAt(i) == '"' ? 2 : 1);
  }
}
