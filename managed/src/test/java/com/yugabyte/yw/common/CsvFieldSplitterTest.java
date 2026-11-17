// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;

import com.yugabyte.yw.common.CsvFieldSplitter.CsvField;
import java.io.IOException;
import java.io.UncheckedIOException;
import java.util.List;
import org.apache.commons.csv.CSVFormat;
import org.apache.commons.csv.CSVParser;
import org.apache.commons.csv.CSVRecord;
import org.junit.Test;

public class CsvFieldSplitterTest {

  @Test
  public void testSplitMatchesCommonsCsv() {
    CSVFormat format = CSVFormat.DEFAULT.builder().setRecordSeparator("").build();
    char[] alphabet = {'a', ',', '"', ' ', '\t'};
    int checked = 0;
    for (int len = 1; len <= 7; len++) {
      int[] digits = new int[len];
      while (true) {
        StringBuilder sb = new StringBuilder();
        for (int d : digits) {
          sb.append(alphabet[d]);
        }
        String value = sb.toString();
        List<String> expected;
        try (CSVParser parser = CSVParser.parse(value, format)) {
          List<CSVRecord> records = parser.getRecords();
          assertEquals(value, 1, records.size());
          expected = records.get(0).toList();
        } catch (IOException | UncheckedIOException e) {
          expected = null;
        }
        List<CsvField> fields = CsvFieldSplitter.split(value);
        assertEquals(
            value, expected, fields == null ? null : fields.stream().map(CsvField::text).toList());
        if (fields != null) {
          for (CsvField field : fields) {
            for (int i = 0; i < field.text().length(); i++) {
              assertEquals(value, field.text().charAt(i), value.charAt(field.rawIndex()[i]));
            }
          }
        }
        checked++;
        int k = len - 1;
        while (k >= 0 && ++digits[k] == alphabet.length) {
          digits[k--] = 0;
        }
        if (k < 0) {
          break;
        }
      }
    }
    assertTrue(checked > 90_000);
  }
}
