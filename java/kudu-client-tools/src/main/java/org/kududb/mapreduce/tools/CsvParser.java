/**
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License. See accompanying LICENSE file.
 */
package org.kududb.mapreduce.tools;

import com.google.common.base.Preconditions;
import com.google.common.base.Splitter;
import com.google.common.collect.Lists;
import org.kududb.annotations.InterfaceAudience;
import org.kududb.annotations.InterfaceStability;
import org.kududb.client.Bytes;

import java.util.ArrayList;
import java.util.List;

/**
 * Column-separated values parser that gives access to the different columns inside each line of
 * data.
 */
@InterfaceAudience.Public
@InterfaceStability.Unstable
public class CsvParser {

  private final byte separatorByte;

  private final int maxColumnCount;

  private final List<String> columnNames;

  /**
   * @param columnsSpecification the list of columns to parse out, comma separated.
   * @param separatorStr The 1 byte separator.
   */
  public CsvParser(String columnsSpecification, String separatorStr) {
    // Configure separator
    byte[] separator = Bytes.fromString(separatorStr);
    Preconditions.checkArgument(separator.length == 1, "CsvParser only supports single-byte " +
        "separators");
    separatorByte = separator[0];

    // Configure columns
    columnNames = Lists.newArrayList(Splitter.on(',').trimResults().split(columnsSpecification));

    maxColumnCount = columnNames.size();
  }

  /**
   * Creates a ParsedLine of a line of data.
   * @param lineBytes Whole line as a byte array.
   * @param length How long the line really is in the byte array
   * @return A parsed line of CSV.
   * @throws BadCsvLineException
   */
  public ParsedLine parse(byte[] lineBytes, int length) throws BadCsvLineException {
    // Enumerate separator offsets
    List<Integer> tabOffsets = new ArrayList<Integer>(maxColumnCount);
    for (int i = 0; i < length; i++) {
      if (lineBytes[i] == separatorByte) {
        tabOffsets.add(i);
      }
    }
    if (tabOffsets.isEmpty()) {
      throw new BadCsvLineException("No delimiter");
    }

    // trailing separator shouldn't count as a column
    if (lineBytes[length - 1] != separatorByte) {
      tabOffsets.add(length);
    }

    if (tabOffsets.size() > maxColumnCount) {
      throw new BadCsvLineException("Excessive columns");
    }

    if (tabOffsets.size() < maxColumnCount) {
      throw new BadCsvLineException("Not enough columns");
    }

    return new ParsedLine(tabOffsets, lineBytes);
  }

  /**
   * Helper class that knows where the columns are situated in the line.
   */
  class ParsedLine {
    private final List<Integer> tabOffsets;
    private final byte[] lineBytes;

    ParsedLine(List<Integer> tabOffsets, byte[] lineBytes) {
      this.tabOffsets = tabOffsets;
      this.lineBytes = lineBytes;
    }

    /**
     * Get the position for the given column.
     * @param idx Column to lookup.
     * @return Offset in the line.
     */
    public int getColumnOffset(int idx) {
      if (idx > 0) {
        return tabOffsets.get(idx - 1) + 1;
      } else {
        return 0;
      }
    }

    /**
     * Get how many bytes the given column occupies.
     * @param idx Column to lookup.
     * @return Column's length.
     */
    public int getColumnLength(int idx) {
      return tabOffsets.get(idx) - getColumnOffset(idx);
    }

    /**
     * Get the number of columns in this file.
     * @return Number of columns.
     */
    public int getColumnCount() {
      return tabOffsets.size();
    }

    /**
     * Get the bytes originally given for this line.
     * @return Original byte array.
     */
    public byte[] getLineBytes() {
      return lineBytes;
    }

    /**
     * Get the given column's name.
     * @param idx Column to lookup.
     * @return Column's name.
     */
    public String getColumnName(int idx) {
      return columnNames.get(idx);
    }
  }

  /**
   * Exception used when the CsvParser is unable to parse a line.
   */
  @SuppressWarnings("serial")
  public static class BadCsvLineException extends Exception {
    public BadCsvLineException(String err) {
      super(err);
    }
  }
}
