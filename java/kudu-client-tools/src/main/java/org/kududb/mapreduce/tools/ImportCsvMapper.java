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

import org.kududb.ColumnSchema;
import org.kududb.Schema;
import org.kududb.annotations.InterfaceAudience;
import org.kududb.annotations.InterfaceStability;
import org.kududb.client.*;
import org.kududb.mapreduce.KuduTableMapReduceUtil;
import org.apache.hadoop.conf.Configuration;
import org.apache.hadoop.io.LongWritable;
import org.apache.hadoop.io.NullWritable;
import org.apache.hadoop.io.Text;
import org.apache.hadoop.mapreduce.Counter;
import org.apache.hadoop.mapreduce.Mapper;

import java.io.IOException;

/**
 * Mapper that ingests CSV lines and turns them into Kudu Inserts.
 */
@InterfaceAudience.Private
@InterfaceStability.Evolving
public class ImportCsvMapper extends Mapper<LongWritable, Text, NullWritable, Operation> {

  private static final NullWritable NULL_KEY = NullWritable.get();

  /** Column seperator */
  private String separator;

  /** Should skip bad lines */
  private boolean skipBadLines;
  private Counter badLineCount;

  private CsvParser parser;

  private KuduTable table;
  private Schema schema;

  /**
   * Handles initializing this class with objects specific to it (i.e., the parser).
   */
  @Override
  protected void setup(Context context) {
    Configuration conf = context.getConfiguration();

    this.separator = conf.get(ImportCsv.SEPARATOR_CONF_KEY);
    if (this.separator == null) {
      this.separator = ImportCsv.DEFAULT_SEPARATOR;
    }

    this.skipBadLines = conf.getBoolean(ImportCsv.SKIP_LINES_CONF_KEY, true);
    this.badLineCount = context.getCounter(ImportCsv.Counters.BAD_LINES);

    this.parser = new CsvParser(conf.get(ImportCsv.COLUMNS_NAMES_KEY), this.separator);

    this.table = KuduTableMapReduceUtil.getTableFromContext(context);
    this.schema = this.table.getSchema();
  }

  /**
   * Convert a line of CSV text into a Kudu Insert
   */
  @Override
  public void map(LongWritable offset, Text value,
                  Context context)
      throws IOException {
    byte[] lineBytes = value.getBytes();

    try {
      CsvParser.ParsedLine parsed = this.parser.parse(lineBytes, value.getLength());

      Insert insert = this.table.newInsert();
      PartialRow row = insert.getRow();
      for (int i = 0; i < parsed.getColumnCount(); i++) {
        String colName = parsed.getColumnName(i);
        ColumnSchema col = this.schema.getColumn(colName);
        String colValue = Bytes.getString(parsed.getLineBytes(), parsed.getColumnOffset(i),
            parsed.getColumnLength(i));
        switch (col.getType()) {
          case BOOL:
            row.addBoolean(colName, Boolean.parseBoolean(colValue));
            break;
          case INT8:
            row.addByte(colName, Byte.parseByte(colValue));
            break;
          case INT16:
            row.addShort(colName, Short.parseShort(colValue));
            break;
          case INT32:
            row.addInt(colName, Integer.parseInt(colValue));
            break;
          case INT64:
            row.addLong(colName, Long.parseLong(colValue));
            break;
          case STRING:
            row.addString(colName, colValue);
            break;
          case FLOAT:
            row.addFloat(colName, Float.parseFloat(colValue));
            break;
          case DOUBLE:
            row.addDouble(colName, Double.parseDouble(colValue));
            break;
          default:
            throw new IllegalArgumentException("Type " + col.getType() + " not recognized");
        }
      }
      context.write(NULL_KEY, insert);
    } catch (CsvParser.BadCsvLineException badLine) {
      if (this.skipBadLines) {
        System.err.println("Bad line at offset: " + offset.get() + ":\n" + badLine.getMessage());
        this.badLineCount.increment(1);
        return;
      } else {
        throw new IOException("Failing task because of a bad line", badLine);
      }
    } catch (IllegalArgumentException e) {
      if (this.skipBadLines) {
        System.err.println("Bad line at offset: " + offset.get() + ":\n" + e.getMessage());
        this.badLineCount.increment(1);
        return;
      } else {
        throw new IOException("Failing task because of an illegal argument", e);
      }
    } catch (InterruptedException e) {
      throw new IOException("Failing task since it was interrupted", e);
    }
  }
}
