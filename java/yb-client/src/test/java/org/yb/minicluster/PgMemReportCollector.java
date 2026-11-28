/**
 * Copyright (c) YugabyteDB, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
 * in compliance with the License.  You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software distributed under the License
 * is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
 * or implied.  See the License for the specific language governing permissions and limitations
 * under the License.
 */
package org.yb.minicluster;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

/**
 * Collects the YB_PG_MEM_* lines that postgres logs when --TEST_ysql_pg_mem_report is on, and
 * reports the postgres processes that went over a --TEST_ysql_pg_mem_*_kb budget. See
 * src/yb/yql/pgwrapper/pg_mem_report.h for the line format.
 */
public class PgMemReportCollector implements LogErrorListener {
  private static final Logger LOG = LoggerFactory.getLogger(PgMemReportCollector.class);

  private static final String START_MARKER = "YB_PG_MEM_START ";
  private static final String RECORD_MARKER = "YB_PG_MEM_RECORD ";
  private static final String SNAPSHOT_MARKER = "YB_PG_MEM_SNAPSHOT ";

  private final List<String> lines = new ArrayList<>();
  private final List<LogPrinter> printers = new ArrayList<>();

  public void attach(LogPrinter printer) {
    synchronized (this) {
      printers.add(printer);
    }
    // Not under this object's lock: the printer calls handleLine() while holding its own lock.
    printer.addErrorListener(this);
  }

  @Override
  public void handleLine(String line) {
    if (line.contains("YB_PG_MEM_")) {
      synchronized (this) {
        lines.add(line);
      }
    }
  }

  @Override
  public void reportErrorsAtEnd() {
  }

  public synchronized List<String> getLines() {
    return new ArrayList<>(lines);
  }

  /**
   * Waits for the output of the attached processes to end, then returns a failure message if a
   * postgres process went over budget and enforcement is on, or null. Logs the report otherwise.
   */
  public String check(long timeoutMs) throws InterruptedException {
    List<LogPrinter> printersCopy;
    synchronized (this) {
      printersCopy = new ArrayList<>(printers);
    }
    long deadlineMs = System.currentTimeMillis() + timeoutMs;
    for (LogPrinter printer : printersCopy) {
      long remainingMs = Math.max(0, deadlineMs - System.currentTimeMillis());
      if (!printer.waitForEndOfStream(remainingMs)) {
        LOG.warn("Postgres output was not fully collected within {} ms", timeoutMs);
        break;
      }
    }

    boolean enforce = false;
    int numRecords = 0;
    // Postmaster pid -> number of records seen for it.
    Map<String, Integer> postmasters = new HashMap<>();
    List<String> violations = new ArrayList<>();
    List<String> warnings = new ArrayList<>();
    for (String line : getLines()) {
      if (line.contains(SNAPSHOT_MARKER)) {
        continue;
      }
      Map<String, String> fields;
      try {
        fields = parse(line);
      } catch (IllegalArgumentException e) {
        warnings.add("Unparsable line: " + line + " (" + e.getMessage() + ")");
        continue;
      }
      String pm = fields.getOrDefault("pm", "?");
      if (line.contains(START_MARKER)) {
        postmasters.putIfAbsent(pm, 0);
        enforce = enforce || "1".equals(fields.get("enforce"));
        continue;
      }
      ++numRecords;
      postmasters.merge(pm, 1, Integer::sum);
      if (!"-".equals(fields.getOrDefault("over", "-"))) {
        violations.add(formatViolation(fields));
      }
    }
    for (Map.Entry<String, Integer> entry : postmasters.entrySet()) {
      if (entry.getValue() == 0) {
        warnings.add("Postmaster " + entry.getKey() + " logged YB_PG_MEM_START but no " +
            "YB_PG_MEM_RECORD lines: records were suppressed (log_min_messages) or lost");
      }
    }

    StringBuilder report = new StringBuilder();
    if (!violations.isEmpty()) {
      report.append(String.format(
          "PG memory budget exceeded (enforce: %s): %d of %d processes over budget%n",
          enforce ? "on" : "off", violations.size(), numRecords));
      for (String violation : violations) {
        report.append(violation);
      }
      report.append("  heap snapshots: search the tserver output for " +
          "'YB_PG_MEM_SNAPSHOT pid=<pid>'\n");
      report.append("  to change a budget: set the --TEST_ysql_pg_mem_*_kb tserver flags\n");
    }
    for (String warning : warnings) {
      report.append("PG memory report warning: ").append(warning).append('\n');
    }
    if (enforce && !violations.isEmpty()) {
      return report.toString();
    }
    if (report.length() > 0) {
      LOG.warn(report.toString());
    }
    return null;
  }

  private static String formatViolation(Map<String, String> r) {
    String over = r.getOrDefault("over", "");
    String type = r.getOrDefault("type", "?");
    String bgw = r.getOrDefault("bgw", "");
    if (!bgw.isEmpty()) {
      type += " (" + bgw + ")";
    }
    StringBuilder result = new StringBuilder(String.format("  pid %s %s db=%s app=\"%s\"%n",
        r.getOrDefault("pid", "?"), type, r.getOrDefault("db", "?"),
        r.getOrDefault("app", "?")));
    if (over.contains("peak")) {
      result.append(String.format("      peak %s kB > budget %s kB (shared %s kB at peak)%n",
          r.get("peak_kb"), r.get("peak_budget_kb"), r.get("peak_shared_kb")));
      String stmt = r.getOrDefault("stmt", "");
      if (!stmt.isEmpty()) {
        result.append("      peak grew after: ").append(stmt).append('\n');
      }
    }
    if (over.contains("idle")) {
      result.append(String.format("      idle anon %s kB > budget %s kB (first idle %s kB)%n",
          r.get("idle_anon_kb"), r.get("idle_budget_kb"), r.get("first_idle_anon_kb")));
    }
    return result.toString();
  }

  /**
   * Parses the key=value pairs after a YB_PG_MEM_* marker. Quoted values may contain \" \\ and \n
   * escapes. The statement is logged hex-encoded as stmt_hex and also returned decoded as stmt.
   */
  static Map<String, String> parse(String line) {
    int start = -1;
    for (String marker : new String[]{RECORD_MARKER, START_MARKER, SNAPSHOT_MARKER}) {
      int pos = line.indexOf(marker);
      if (pos >= 0) {
        start = pos + marker.length();
        break;
      }
    }
    if (start < 0) {
      throw new IllegalArgumentException("No YB_PG_MEM marker");
    }
    Map<String, String> result = new HashMap<>();
    int i = start;
    int n = line.length();
    while (i < n) {
      char c = line.charAt(i);
      if (c == ' ' || c == '\n' || c == '\r') {
        ++i;
        continue;
      }
      int eq = line.indexOf('=', i);
      if (eq < 0) {
        throw new IllegalArgumentException("Expected key=value at offset " + i);
      }
      String key = line.substring(i, eq);
      i = eq + 1;
      StringBuilder value = new StringBuilder();
      if (i < n && line.charAt(i) == '"') {
        ++i;
        boolean closed = false;
        while (i < n) {
          char v = line.charAt(i++);
          if (v == '"') {
            closed = true;
            break;
          }
          if (v == '\\' && i < n) {
            char escaped = line.charAt(i++);
            value.append(escaped == 'n' ? '\n' : escaped);
          } else {
            value.append(v);
          }
        }
        if (!closed) {
          throw new IllegalArgumentException("Unterminated quoted value for " + key);
        }
      } else {
        int end = line.indexOf(' ', i);
        if (end < 0) {
          end = n;
        }
        value.append(line, i, end);
        i = end;
      }
      if (key.equals("stmt_hex")) {
        result.put("stmt", decodeHex(value.toString()));
      }
      result.put(key, value.toString());
    }
    return result;
  }

  private static String decodeHex(String hex) {
    if (hex.length() % 2 != 0) {
      throw new IllegalArgumentException("Odd-length hex value: " + hex);
    }
    byte[] bytes = new byte[hex.length() / 2];
    for (int i = 0; i < bytes.length; i++) {
      int hi = Character.digit(hex.charAt(2 * i), 16);
      int lo = Character.digit(hex.charAt(2 * i + 1), 16);
      if (hi < 0 || lo < 0) {
        throw new IllegalArgumentException("Bad hex value: " + hex);
      }
      bytes[i] = (byte) (hi << 4 | lo);
    }
    return new String(bytes, java.nio.charset.StandardCharsets.UTF_8);
  }
}
