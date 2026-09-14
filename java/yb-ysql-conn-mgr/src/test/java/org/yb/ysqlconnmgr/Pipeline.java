// Copyright (c) YugabyteDB, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License. You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied. See the License for the specific language governing permissions and limitations
// under the License.
//

package org.yb.ysqlconnmgr;

import static org.yb.AssertionWrappers.fail;
import static org.yb.ysqlconnmgr.PgWireProtocol.BE_COMMAND_COMPLETE;
import static org.yb.ysqlconnmgr.PgWireProtocol.BE_COPY_DATA;
import static org.yb.ysqlconnmgr.PgWireProtocol.BE_COPY_DONE;
import static org.yb.ysqlconnmgr.PgWireProtocol.BE_COPY_IN_RESPONSE;
import static org.yb.ysqlconnmgr.PgWireProtocol.BE_COPY_OUT_RESPONSE;
import static org.yb.ysqlconnmgr.PgWireProtocol.BE_DATA_ROW;
import static org.yb.ysqlconnmgr.PgWireProtocol.BE_EMPTY_QUERY_RESPONSE;
import static org.yb.ysqlconnmgr.PgWireProtocol.BE_ERROR_RESPONSE;
import static org.yb.ysqlconnmgr.PgWireProtocol.BE_FUNCTION_CALL_RESPONSE;
import static org.yb.ysqlconnmgr.PgWireProtocol.BE_NOTICE_RESPONSE;
import static org.yb.ysqlconnmgr.PgWireProtocol.BE_PARAMETER_STATUS;
import static org.yb.ysqlconnmgr.PgWireProtocol.BE_READY_FOR_QUERY;
import static org.yb.ysqlconnmgr.PgWireProtocol.BE_ROW_DESCRIPTION;

import java.io.ByteArrayOutputStream;
import java.io.EOFException;
import java.io.IOException;
import java.net.SocketTimeoutException;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.ysqlconnmgr.PgWireProtocol.PgMessage;

/**
 * Builds a pipeline of frontend messages, sends it and asserts what the backend
 * answers. Each step carries the response it gets on success; an inline
 * {@code expectError(...)} replaces that with one ErrorResponse and puts the
 * builder into skip mode until the next {@code sync()}, mirroring the backend's
 * ignore-till-sync rule.
 *
 * <pre>
 * Step                                   Sends  Expects
 * parse(sql) / parse(name, sql, oids)    P      1
 * bind() / bind(stmt, params...)         B      2
 * execute()                              E      D* C
 * closeStmt(name)                        C      3
 * sync()                                 S      Z
 * query(sql)                             Q      (T? D* C | I)+ Z
 * functionCall(oid)                      F      V Z
 * copyData(s) / copyDone() / copyFail()  d c f  nothing / C / E
 * </pre>
 *
 * {@code sync()}, {@code query()} and {@code functionCall()} are read
 * boundaries: a step's expectation is read at the next boundary at or after it.
 * A pipeline whose trailing steps expect a response that no boundary reads
 * fails, unless it declares {@code allowUnreadTail()}. NoticeResponse and
 * ParameterStatus are skipped everywhere.
 *
 * <pre>
 * try (WireConn c = rawConnBuilder().connect()) {
 *   PipelineResult r = c.createPipeline()
 *       .parse("S1", "INSERT INTO t VALUES (42)").bind("S1").execute().tag("INSERT 0 1")
 *       .parse("S2", "SELECT * FROM t").bind("S2").execute().row("42").label("all")
 *       .parse("S_WRONG", "INSERT INTO t VALUES (42, 43)")
 *           .expectError("INSERT has more expressions than target columns")
 *       .bind("S_WRONG").execute()
 *       .sync()
 *       .run();
 *   // sent   P(S1)+B(S1)+E+P(S2)+B(S2)+E+P(S_WRONG)+B(S_WRONG)+E+Sync
 *   // expect 1 2 C 1 2 D{1} C E Z
 *   assertEquals("42", r.value("all"));
 * }
 * </pre>
 */
public class Pipeline {
  private static final Logger LOG = LoggerFactory.getLogger(Pipeline.class);

  private enum Kind {
    PARSE, BIND, EXECUTE, CLOSE, SYNC, QUERY, FUNCTION_CALL, COPY_DATA, COPY_DONE, COPY_FAIL, PAUSE
  }

  private enum CopyOverride { NONE, COPY_IN, COPY_OUT, IGNORED }

  private static class Step {
    final Kind kind;
    final String notation;
    final String call;
    final byte[] bytes;
    boolean boundary;
    boolean skipped;
    String label;

    Integer rowCount;
    final List<List<String>> expectedRows = new ArrayList<>();
    String tag;

    boolean expectsError;
    String errSqlState;
    String errSubstring;

    CopyOverride copy = CopyOverride.NONE;
    int copyOutRows;

    long pauseMs;

    final List<PgMessage> received = new ArrayList<>();
    List<List<String>> dataRows = new ArrayList<>();

    Step(Kind kind, String notation, String call, byte[] bytes) {
      this.kind = kind;
      this.notation = notation;
      this.call = call;
      this.bytes = bytes;
    }
  }

  private static class Failure extends RuntimeException {
    Failure(String message) {
      super(message);
    }
  }

  private final WireConn conn;
  private final List<Step> steps = new ArrayList<>();
  private final List<PgMessage> transcript = new ArrayList<>();
  private boolean skipMode;
  private boolean allowUnreadTail;
  private PgMessage pushback;
  private Step currentStep;

  Pipeline(WireConn conn) {
    this.conn = conn;
  }

  // ---- Building: extended query protocol ------------------------------------

  public Pipeline parse(String sql) {
    return add(Kind.PARSE, "P", "parse(" + abbreviate(sql) + ")",
        bytes(() -> PgWireProtocol.buildParse(sql)));
  }

  public Pipeline parse(String stmtName, String sql, int... paramOids) {
    return add(Kind.PARSE, "P(" + stmtName + ")", "parse(" + stmtName + ")",
        bytes(() -> PgWireProtocol.buildParse(stmtName, sql, paramOids)));
  }

  public Pipeline bind() {
    return add(Kind.BIND, "B", "bind()", bytes(() -> PgWireProtocol.buildBind()));
  }

  public Pipeline bind(String stmtName, String... params) {
    return add(Kind.BIND, "B(" + stmtName + ")", "bind(" + stmtName + ")",
        bytes(() -> PgWireProtocol.buildBind(stmtName, params)));
  }

  public Pipeline execute() {
    return add(Kind.EXECUTE, "E", "execute()", bytes(() -> PgWireProtocol.buildExecute()));
  }

  public Pipeline closeStmt(String stmtName) {
    return add(Kind.CLOSE, "C(S:" + stmtName + ")", "closeStmt(" + stmtName + ")",
        bytes(() -> PgWireProtocol.buildClose('S', stmtName)));
  }

  public Pipeline sync() {
    add(Kind.SYNC, "Sync", "sync()", bytes(() -> PgWireProtocol.buildSync()));
    last().boundary = true;
    last().skipped = false;
    skipMode = false;
    return this;
  }

  public Pipeline query(String sql) {
    add(Kind.QUERY, "Q(" + abbreviate(sql) + ")", "query(" + abbreviate(sql) + ")",
        bytes(() -> PgWireProtocol.buildQuery(sql)));
    last().boundary = !last().skipped;
    return this;
  }

  public Pipeline functionCall(int functionOid) {
    add(Kind.FUNCTION_CALL, "F(" + functionOid + ")", "functionCall(" + functionOid + ")",
        bytes(() -> PgWireProtocol.buildFunctionCall(functionOid)));
    last().boundary = !last().skipped;
    return this;
  }

  // ---- Building: COPY -------------------------------------------------------

  public Pipeline copyData(String data) {
    return add(Kind.COPY_DATA, "d", "copyData()",
        bytes(() -> PgWireProtocol.buildCopyData(data)));
  }

  public Pipeline copyDone() {
    return add(Kind.COPY_DONE, "c", "copyDone()", bytes(() -> PgWireProtocol.buildCopyDone()));
  }

  public Pipeline copyFail(String message) {
    return add(Kind.COPY_FAIL, "f", "copyFail()",
        bytes(() -> PgWireProtocol.buildCopyFail(message)));
  }

  public Pipeline expectCopyIn() {
    requireKind("expectCopyIn", Kind.EXECUTE);
    last().copy = CopyOverride.COPY_IN;
    return this;
  }

  public Pipeline expectCopyOut(int numRows) {
    requireKind("expectCopyOut", Kind.EXECUTE);
    last().copy = CopyOverride.COPY_OUT;
    last().copyOutRows = numRows;
    return this;
  }

  public Pipeline ignoredInCopyMode() {
    requireKind("ignoredInCopyMode", Kind.SYNC);
    last().copy = CopyOverride.IGNORED;
    return this;
  }

  // ---- Building: misc -------------------------------------------------------

  public Pipeline pause(long millis) {
    add(Kind.PAUSE, "pause(" + millis + ")", "pause(" + millis + ")", new byte[0]);
    last().pauseMs = millis;
    last().skipped = false;
    return this;
  }

  // ---- Building: expectations ----------------------------------------------

  public Pipeline expectError(String substring) {
    return expectError(null, substring);
  }

  public Pipeline expectError(String sqlState, String substring) {
    Step step = last();
    if (step.kind == Kind.SYNC) {
      throw new IllegalStateException("expectError is not allowed on " + step.call);
    }
    if (step.expectsError) {
      throw new IllegalStateException("two expectError on one step: " + step.call);
    }
    step.expectsError = true;
    step.errSqlState = sqlState;
    step.errSubstring = substring;
    if (isExtendedQueryStep(step.kind)) {
      skipMode = true;
    }
    return this;
  }

  public Pipeline rowCount(int numRows) {
    requireResultKind("rowCount", Kind.EXECUTE, Kind.QUERY);
    last().rowCount = numRows;
    return this;
  }

  public Pipeline row(String... values) {
    requireResultKind("row", Kind.EXECUTE, Kind.QUERY);
    last().expectedRows.add(Arrays.asList(values));
    return this;
  }

  public Pipeline tag(String commandTag) {
    requireResultKind("tag", Kind.EXECUTE, Kind.QUERY, Kind.COPY_DONE);
    last().tag = commandTag;
    return this;
  }

  public Pipeline label(String name) {
    requireResultKind("label", Kind.EXECUTE, Kind.QUERY);
    last().label = name;
    return this;
  }

  /**
   * Declares that the trailing steps are deliberately left unanswered, the only
   * legitimate reason being that the backend was never asked to flush them.
   * Leaves the connection desynchronized, so it cannot run another pipeline.
   */
  public Pipeline allowUnreadTail() {
    allowUnreadTail = true;
    return this;
  }

  // ---- Running --------------------------------------------------------------

  public PipelineResult run() throws Exception {
    LOG.info("sent   {}", sentNotation());
    LOG.info("expect {}", expectedNotation());

    try {
      int pending = 0;
      int segmentStart = 0;
      for (int i = 0; i <= steps.size(); i++) {
        boolean end = i == steps.size();
        if (!end && steps.get(i).kind != Kind.PAUSE) {
          continue;
        }
        writeSegment(segmentStart, i);
        pending = readSegment(pending, i);
        if (!end) {
          Thread.sleep(steps.get(i).pauseMs);
          segmentStart = i + 1;
        }
      }
      finish(pending);
    } catch (Failure failure) {
      fail(failure.getMessage() + "\n" + dump());
    }
    return buildResult();
  }

  private void writeSegment(int from, int to) throws IOException {
    ByteArrayOutputStream buf = new ByteArrayOutputStream();
    for (int i = from; i < to; i++) {
      buf.write(steps.get(i).bytes);
    }
    if (buf.size() > 0) {
      conn.sendRaw(buf.toByteArray());
    }
  }

  private int readSegment(int pending, int segmentEnd) {
    int lastBoundary = -1;
    for (int i = pending; i < segmentEnd; i++) {
      if (steps.get(i).boundary) {
        lastBoundary = i;
      }
    }
    if (lastBoundary < 0) {
      return pending;
    }
    for (int i = pending; i <= lastBoundary; i++) {
      currentStep = steps.get(i);
      readStep(currentStep);
    }
    currentStep = null;
    return lastBoundary + 1;
  }

  private void finish(int pending) throws IOException {
    List<String> unread = new ArrayList<>();
    for (int i = pending; i < steps.size(); i++) {
      Step step = steps.get(i);
      if (!expectedTokens(step).isEmpty()) {
        unread.add("step " + i + " " + step.notation);
      }
    }
    if (!unread.isEmpty()) {
      if (!allowUnreadTail) {
        throw new Failure("no read boundary after " + unread
            + "; their expectations would never be checked. End the pipeline with sync(), or"
            + " declare allowUnreadTail() if the backend is not meant to answer");
      }
      // The unread responses are still queued and would be misread as the next pipeline's.
      conn.markTailUnread();
      return;
    }
    if (conn.available() != 0) {
      throw new Failure("unexpected trailing bytes after the pipeline: "
          + conn.available() + " bytes");
    }
  }

  // ---- Reading one step -----------------------------------------------------

  private void readStep(Step step) {
    if (step.expectsError) {
      ErrorResponse error = ErrorResponse.parse(expect(step, BE_ERROR_RESPONSE).body);
      if (step.errSqlState != null && !step.errSqlState.equals(error.sqlstate())) {
        throw failure(step, "expected sqlstate " + step.errSqlState + ", got " + error);
      }
      if (step.errSubstring != null && (error.message() == null
          || !error.message().contains(step.errSubstring))) {
        throw failure(step, "expected an error containing \"" + step.errSubstring
            + "\", got " + error);
      }
      if (step.kind == Kind.QUERY || step.kind == Kind.FUNCTION_CALL) {
        expect(step, BE_READY_FOR_QUERY);
      }
      return;
    }
    if (step.skipped) {
      return;
    }
    switch (step.kind) {
      case PARSE:
        expect(step, PgWireProtocol.BE_PARSE_COMPLETE);
        break;
      case BIND:
        expect(step, PgWireProtocol.BE_BIND_COMPLETE);
        break;
      case CLOSE:
        expect(step, PgWireProtocol.BE_CLOSE_COMPLETE);
        break;
      case EXECUTE:
        readExecute(step);
        break;
      case SYNC:
        if (step.copy != CopyOverride.IGNORED) {
          expect(step, BE_READY_FOR_QUERY);
        }
        break;
      case QUERY:
        readQuery(step);
        break;
      case FUNCTION_CALL:
        expect(step, BE_FUNCTION_CALL_RESPONSE);
        expect(step, BE_READY_FOR_QUERY);
        break;
      case COPY_DONE:
        checkTag(step, expect(step, BE_COMMAND_COMPLETE));
        break;
      case COPY_FAIL:
        expect(step, BE_ERROR_RESPONSE);
        break;
      default:
        break;
    }
  }

  private void readExecute(Step step) {
    switch (step.copy) {
      case COPY_IN:
        expect(step, BE_COPY_IN_RESPONSE);
        return;
      case COPY_OUT:
        expect(step, BE_COPY_OUT_RESPONSE);
        for (int i = 0; i < step.copyOutRows; i++) {
          expect(step, BE_COPY_DATA);
        }
        expect(step, BE_COPY_DONE);
        checkTag(step, expect(step, BE_COMMAND_COMPLETE));
        return;
      default:
        break;
    }
    readDataRows(step);
    checkTag(step, expect(step, BE_COMMAND_COMPLETE));
  }

  private void readQuery(Step step) {
    List<List<String>> rows = new ArrayList<>();
    for (;;) {
      PgMessage msg = nextMessage();
      if (msg.type == BE_READY_FOR_QUERY) {
        break;
      }
      switch (msg.type) {
        case BE_ROW_DESCRIPTION:
        case BE_EMPTY_QUERY_RESPONSE:
          break;
        case BE_DATA_ROW:
          rows.add(PgWireProtocol.parseDataRow(msg.body));
          break;
        case BE_COMMAND_COMPLETE:
          checkTag(step, msg);
          break;
        default:
          throw failure(step, "unexpected message in the simple-query response: "
              + describe(msg));
      }
    }
    step.dataRows = rows;
    checkRows(step);
  }

  private void readDataRows(Step step) {
    List<List<String>> rows = new ArrayList<>();
    for (;;) {
      PgMessage msg = nextMessage();
      if (msg.type != BE_DATA_ROW) {
        pushBack(msg);
        break;
      }
      rows.add(PgWireProtocol.parseDataRow(msg.body));
    }
    step.dataRows = rows;
    checkRows(step);
  }

  private void checkRows(Step step) {
    if (!step.expectedRows.isEmpty()) {
      if (step.dataRows.size() != step.expectedRows.size()) {
        throw failure(step, "expected " + step.expectedRows.size() + " rows, got "
            + step.dataRows.size() + " " + step.dataRows);
      }
      for (int i = 0; i < step.expectedRows.size(); i++) {
        if (!step.expectedRows.get(i).equals(step.dataRows.get(i))) {
          throw failure(step, "row " + i + ": expected " + step.expectedRows.get(i)
              + ", got " + step.dataRows.get(i));
        }
      }
    }
    if (step.rowCount != null && step.dataRows.size() != step.rowCount) {
      throw failure(step, "expected " + step.rowCount + " rows, got " + step.dataRows.size());
    }
  }

  private void checkTag(Step step, PgMessage msg) {
    if (step.tag == null) {
      return;
    }
    String tag = PgWireProtocol.parseCommandComplete(msg.body);
    if (!step.tag.equals(tag)) {
      throw failure(step, "expected command tag \"" + step.tag + "\", got \"" + tag + "\"");
    }
  }

  private PgMessage expect(Step step, char... allowed) {
    PgMessage msg = nextMessage();
    for (char type : allowed) {
      if (msg.type == type) {
        return msg;
      }
    }
    StringBuilder names = new StringBuilder();
    for (char type : allowed) {
      if (names.length() > 0) {
        names.append(" or ");
      }
      names.append(new PgMessage(type, new byte[0]).typeToString());
    }
    throw failure(step, "expected " + names + ", got " + describe(msg));
  }

  private PgMessage nextMessage() {
    for (;;) {
      PgMessage msg;
      if (pushback != null) {
        msg = pushback;
        pushback = null;
      } else {
        try {
          msg = conn.readMessage();
        } catch (EOFException e) {
          throw new Failure("connection closed by peer");
        } catch (SocketTimeoutException e) {
          throw new Failure("timed out after " + transcript.size() + " of "
              + expectedNotation().split(" ").length + " messages");
        } catch (IOException e) {
          throw new Failure("read failed: " + e);
        }
        transcript.add(msg);
        LOG.debug("received {}", describe(msg));
      }
      if (currentStep != null) {
        currentStep.received.add(msg);
      }
      // Both are asynchronous: they can land at any point in the stream.
      if (msg.type == BE_NOTICE_RESPONSE || msg.type == BE_PARAMETER_STATUS) {
        continue;
      }
      return msg;
    }
  }

  private void pushBack(PgMessage msg) {
    pushback = msg;
    if (currentStep != null && !currentStep.received.isEmpty()) {
      currentStep.received.remove(currentStep.received.size() - 1);
    }
  }

  // ---- Reporting ------------------------------------------------------------

  private Failure failure(Step step, String what) {
    return new Failure("step " + steps.indexOf(step) + " " + step.call + ": " + what);
  }

  private String dump() {
    StringBuilder sb = new StringBuilder("pipeline transcript:");
    for (int i = 0; i < steps.size(); i++) {
      Step step = steps.get(i);
      sb.append(String.format("%n  %s step %-3d %-24s expected [%s] got [%s]",
          step == currentStep ? "->" : "  ", i, step.notation,
          expectedTokens(step), describeAll(step.received)));
    }
    return sb.toString();
  }

  private String sentNotation() {
    StringBuilder sb = new StringBuilder();
    for (Step step : steps) {
      if (step.notation.isEmpty()) {
        continue;
      }
      if (sb.length() > 0) {
        sb.append('+');
      }
      sb.append(step.notation);
    }
    return sb.toString();
  }

  private String expectedNotation() {
    StringBuilder sb = new StringBuilder();
    for (Step step : steps) {
      String tokens = expectedTokens(step);
      if (tokens.isEmpty()) {
        continue;
      }
      if (sb.length() > 0) {
        sb.append(' ');
      }
      sb.append(tokens);
    }
    return sb.toString();
  }

  private String expectedTokens(Step step) {
    if (step.expectsError) {
      return step.kind == Kind.QUERY || step.kind == Kind.FUNCTION_CALL ? "E Z" : "E";
    }
    if (step.skipped) {
      return "";
    }
    String rows = step.expectedRows.isEmpty()
        ? (step.rowCount == null ? "D*" : "D{" + step.rowCount + "}")
        : "D{" + step.expectedRows.size() + "}";
    switch (step.kind) {
      case PARSE: return "1";
      case BIND: return "2";
      case CLOSE: return "3";
      case EXECUTE:
        if (step.copy == CopyOverride.COPY_IN) {
          return "G";
        }
        if (step.copy == CopyOverride.COPY_OUT) {
          return "H d{" + step.copyOutRows + "} c C";
        }
        return rows + " C";
      case SYNC: return step.copy == CopyOverride.IGNORED ? "" : "Z";
      case QUERY: return "T? " + rows + " C Z";
      case FUNCTION_CALL: return "V Z";
      case COPY_DONE: return "C";
      case COPY_FAIL: return "E";
      default: return "";
    }
  }

  private static String describeAll(List<PgMessage> messages) {
    StringBuilder sb = new StringBuilder();
    for (PgMessage msg : messages) {
      if (sb.length() > 0) {
        sb.append(", ");
      }
      sb.append(describe(msg));
    }
    return sb.toString();
  }

  private static String describe(PgMessage msg) {
    switch (msg.type) {
      case BE_ERROR_RESPONSE:
      case BE_NOTICE_RESPONSE:
        return msg.typeToString() + " " + ErrorResponse.parse(msg.body);
      case BE_COMMAND_COMPLETE:
        return msg.typeToString() + " \"" + PgWireProtocol.parseCommandComplete(msg.body) + "\"";
      case BE_DATA_ROW:
        return msg.typeToString() + " " + PgWireProtocol.parseDataRow(msg.body);
      default:
        return msg.typeToString();
    }
  }

  private PipelineResult buildResult() {
    Map<String, List<List<String>>> rowsByLabel = new LinkedHashMap<>();
    for (Step step : steps) {
      if (step.label != null) {
        rowsByLabel.put(step.label, step.dataRows);
      }
    }
    return new PipelineResult(rowsByLabel);
  }

  // ---- Building helpers -----------------------------------------------------

  private interface ByteBuilder {
    byte[] build() throws IOException;
  }

  private static byte[] bytes(ByteBuilder builder) {
    try {
      return builder.build();
    } catch (IOException e) {
      throw new RuntimeException("Failed to build a wire message", e);
    }
  }

  private Pipeline add(Kind kind, String notation, String call, byte[] payload) {
    Step step = new Step(kind, notation, call, payload);
    step.skipped = skipMode;
    steps.add(step);
    return this;
  }

  private Step last() {
    if (steps.isEmpty()) {
      throw new IllegalStateException("No step to attach this expectation to");
    }
    return steps.get(steps.size() - 1);
  }

  private void requireKind(String method, Kind... kinds) {
    if (!Arrays.asList(kinds).contains(last().kind)) {
      throw new IllegalStateException(method + " does not apply to " + last().call);
    }
  }

  /** {@link #requireKind}, plus a step that expects an error never checks a result. */
  private void requireResultKind(String method, Kind... kinds) {
    requireKind(method, kinds);
    if (last().expectsError) {
      throw new IllegalStateException(method + " does not apply to " + last().call
          + ", which expects an error");
    }
  }

  private static boolean isExtendedQueryStep(Kind kind) {
    switch (kind) {
      case PARSE:
      case BIND:
      case EXECUTE:
      case CLOSE:
        return true;
      default:
        return false;
    }
  }

  private static String abbreviate(String sql) {
    String oneLine = sql.replaceAll("\\s+", " ").trim();
    return "'" + (oneLine.length() > 30 ? oneLine.substring(0, 27) + "..." : oneLine) + "'";
  }
}
