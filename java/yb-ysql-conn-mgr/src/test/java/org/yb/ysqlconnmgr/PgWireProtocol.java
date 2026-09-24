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

import java.io.ByteArrayOutputStream;
import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

/**
 * Utility class for building and reading PostgreSQL v3 wire-protocol messages
 * over a raw TCP socket. Intended for tests that need to exercise protocol
 * behaviour that standard JDBC drivers cannot produce (e.g. pipelined
 * extended-query sequences with intermediate Sync messages).
 */
public final class PgWireProtocol {
  private static final Logger LOG = LoggerFactory.getLogger(PgWireProtocol.class);

  private static final int MAX_MESSAGE_LENGTH = 64 * 1024 * 1024;

  private PgWireProtocol() {}

  // -- Wire-protocol message types (backend) ----------------------------------
  public static final char BE_AUTHENTICATION = 'R';
  public static final char BE_PARAMETER_STATUS = 'S';
  public static final char BE_BACKEND_KEY_DATA = 'K';
  public static final char BE_READY_FOR_QUERY = 'Z';
  public static final char BE_PARSE_COMPLETE = '1';
  public static final char BE_BIND_COMPLETE = '2';
  public static final char BE_CLOSE_COMPLETE = '3';
  public static final char BE_DATA_ROW = 'D';
  public static final char BE_COMMAND_COMPLETE = 'C';
  public static final char BE_ERROR_RESPONSE = 'E';
  public static final char BE_COPY_IN_RESPONSE = 'G';
  public static final char BE_COPY_OUT_RESPONSE = 'H';
  public static final char BE_COPY_DATA = 'd';
  public static final char BE_COPY_DONE = 'c';
  public static final char BE_NOTICE_RESPONSE = 'N';
  public static final char BE_ROW_DESCRIPTION = 'T';
  public static final char BE_PARAMETER_DESCRIPTION = 't';
  public static final char BE_NO_DATA = 'n';
  public static final char BE_FUNCTION_CALL_RESPONSE = 'V';
  public static final char BE_EMPTY_QUERY_RESPONSE = 'I';

  // ---- Tiny record for a backend message ------------------------------------
  public static class PgMessage {
    public final char type;
    public final byte[] body;

    public PgMessage(char type, byte[] body) {
      this.type = type;
      this.body = body;
    }

    public String typeToString() {
      switch (type) {
        case 'R': return "Authentication";
        case 'S': return "ParameterStatus";
        case 'K': return "BackendKeyData";
        case 'Z': return "ReadyForQuery";
        case '1': return "ParseComplete";
        case '2': return "BindComplete";
        case '3': return "CloseComplete";
        case 't': return "ParameterDescription";
        case 'n': return "NoData";
        case 's': return "PortalSuspended";
        case 'V': return "FunctionCallResponse";
        case 'I': return "EmptyQueryResponse";
        case 'D': return "DataRow";
        case 'C': return "CommandComplete";
        case 'E': return "ErrorResponse";
        case 'G': return "CopyInResponse";
        case 'H': return "CopyOutResponse";
        case 'd': return "CopyData";
        case 'c': return "CopyDone";
        case 'N': return "NoticeResponse";
        case 'T': return "RowDescription";
        default:  return "Unknown('" + type + "')";
      }
    }

    @Override
    public String toString() {
      return "PgMessage[" + typeToString() + ", len=" + body.length + "]";
    }
  }

  // ---- Wire helpers: building frontend messages -----------------------------

  public static byte[] buildStartupMessage(
      String user, String database, Map<String, String> parameters) throws IOException {
    ByteArrayOutputStream buf = new ByteArrayOutputStream();
    DataOutputStream d = new DataOutputStream(buf);
    d.writeInt(0); // placeholder for length
    d.writeInt(196608); // protocol version 3.0
    writeString(d, "user");
    writeString(d, user);
    writeString(d, "database");
    writeString(d, database);
    if (parameters != null) {
      for (Map.Entry<String, String> entry : parameters.entrySet()) {
        writeString(d, entry.getKey());
        writeString(d, entry.getValue());
      }
    }
    d.writeByte(0); // terminator
    d.flush();
    byte[] msg = buf.toByteArray();
    ByteBuffer.wrap(msg).putInt(0, msg.length);
    return msg;
  }

  public static byte[] buildParse(String query) throws IOException {
    ByteArrayOutputStream buf = new ByteArrayOutputStream();
    DataOutputStream d = new DataOutputStream(buf);
    d.writeByte('P');
    d.writeInt(0); // placeholder
    d.writeByte(0); // unnamed statement
    writeString(d, query);
    d.writeShort(0); // no parameter types
    d.flush();
    byte[] msg = buf.toByteArray();
    ByteBuffer.wrap(msg).putInt(1, msg.length - 1);
    return msg;
  }

  public static byte[] buildParse(String stmtName, String query, int[] paramOids)
      throws IOException {
    ByteArrayOutputStream buf = new ByteArrayOutputStream();
    DataOutputStream d = new DataOutputStream(buf);
    d.writeByte('P');
    d.writeInt(0); // placeholder
    writeString(d, stmtName);
    writeString(d, query);
    d.writeShort(paramOids.length);
    for (int oid : paramOids) {
      d.writeInt(oid);
    }
    d.flush();
    byte[] msg = buf.toByteArray();
    ByteBuffer.wrap(msg).putInt(1, msg.length - 1);
    return msg;
  }

  public static byte[] buildBind() throws IOException {
    return buildBind("", new String[0]);
  }

  /** Binds the unnamed portal; a null entry in {@code textParams} is sent as SQL NULL. */
  public static byte[] buildBind(String stmtName, String[] textParams) throws IOException {
    return buildBind("", stmtName, textParams);
  }

  public static byte[] buildBind(String portalName, String stmtName, String[] textParams)
      throws IOException {
    ByteArrayOutputStream buf = new ByteArrayOutputStream();
    DataOutputStream d = new DataOutputStream(buf);
    d.writeByte('B');
    d.writeInt(0); // placeholder
    writeString(d, portalName);
    writeString(d, stmtName);
    d.writeShort(0); // num format codes (all text)
    d.writeShort(textParams.length);
    for (String param : textParams) {
      if (param == null) {
        d.writeInt(-1);
        continue;
      }
      byte[] paramBytes = param.getBytes(StandardCharsets.UTF_8);
      d.writeInt(paramBytes.length);
      d.write(paramBytes);
    }
    d.writeShort(0); // num result format codes
    d.flush();
    byte[] msg = buf.toByteArray();
    ByteBuffer.wrap(msg).putInt(1, msg.length - 1);
    return msg;
  }

  public static byte[] buildExecute() throws IOException {
    return buildExecute("");
  }

  public static byte[] buildExecute(String portalName) throws IOException {
    ByteArrayOutputStream buf = new ByteArrayOutputStream();
    DataOutputStream d = new DataOutputStream(buf);
    d.writeByte('E');
    d.writeInt(0); // placeholder
    writeString(d, portalName);
    d.writeInt(0); // max rows (0 = unlimited)
    d.flush();
    byte[] msg = buf.toByteArray();
    ByteBuffer.wrap(msg).putInt(1, msg.length - 1);
    return msg;
  }

  public static byte[] buildSync() throws IOException {
    ByteArrayOutputStream buf = new ByteArrayOutputStream();
    DataOutputStream d = new DataOutputStream(buf);
    d.writeByte('S');
    d.writeInt(4);
    d.flush();
    return buf.toByteArray();
  }

  public static byte[] buildCopyData(String data) throws IOException {
    ByteArrayOutputStream buf = new ByteArrayOutputStream();
    DataOutputStream d = new DataOutputStream(buf);
    d.writeByte('d');
    d.writeInt(0); // placeholder
    d.write(data.getBytes(StandardCharsets.UTF_8));
    d.flush();
    byte[] msg = buf.toByteArray();
    ByteBuffer.wrap(msg).putInt(1, msg.length - 1);
    return msg;
  }

  public static byte[] buildCopyFail(String errorMessage) throws IOException {
    ByteArrayOutputStream buf = new ByteArrayOutputStream();
    DataOutputStream d = new DataOutputStream(buf);
    d.writeByte('f');
    d.writeInt(0); // placeholder
    writeString(d, errorMessage);
    d.flush();
    byte[] msg = buf.toByteArray();
    ByteBuffer.wrap(msg).putInt(1, msg.length - 1);
    return msg;
  }

  public static byte[] buildCopyDone() throws IOException {
    ByteArrayOutputStream buf = new ByteArrayOutputStream();
    DataOutputStream d = new DataOutputStream(buf);
    d.writeByte('c');
    d.writeInt(4);
    d.flush();
    return buf.toByteArray();
  }

  public static byte[] buildQuery(String query) throws IOException {
    ByteArrayOutputStream buf = new ByteArrayOutputStream();
    DataOutputStream d = new DataOutputStream(buf);
    d.writeByte('Q');
    d.writeInt(0); // placeholder
    writeString(d, query);
    d.flush();
    byte[] msg = buf.toByteArray();
    ByteBuffer.wrap(msg).putInt(1, msg.length - 1);
    return msg;
  }

  public static byte[] buildFunctionCall(int functionOid) throws IOException {
    ByteArrayOutputStream buf = new ByteArrayOutputStream();
    DataOutputStream d = new DataOutputStream(buf);
    d.writeByte('F');
    d.writeInt(0); // placeholder
    d.writeInt(functionOid);
    d.writeShort(0); // no argument format codes
    d.writeShort(0); // no arguments
    d.writeShort(0); // result format code (text)
    d.flush();
    byte[] msg = buf.toByteArray();
    ByteBuffer.wrap(msg).putInt(1, msg.length - 1);
    return msg;
  }

  public static byte[] buildTerminate() throws IOException {
    ByteArrayOutputStream buf = new ByteArrayOutputStream();
    DataOutputStream d = new DataOutputStream(buf);
    d.writeByte('X');
    d.writeInt(4);
    d.flush();
    return buf.toByteArray();
  }

  public static byte[] buildClose(char kind, String name) throws IOException {
    return buildDescribeOrClose('C', kind, name);
  }

  public static byte[] buildDescribe(char kind, String name) throws IOException {
    return buildDescribeOrClose('D', kind, name);
  }

  private static byte[] buildDescribeOrClose(char msgType, char kind, String name)
      throws IOException {
    ByteArrayOutputStream buf = new ByteArrayOutputStream();
    DataOutputStream d = new DataOutputStream(buf);
    d.writeByte(msgType);
    d.writeInt(0); // placeholder
    d.writeByte(kind);
    writeString(d, name);
    d.flush();
    byte[] msg = buf.toByteArray();
    ByteBuffer.wrap(msg).putInt(1, msg.length - 1);
    return msg;
  }

  // Builds a message of an arbitrary type, for packets no well-formed client sends.
  public static byte[] buildRaw(char type, byte[] body) throws IOException {
    ByteArrayOutputStream buf = new ByteArrayOutputStream();
    DataOutputStream d = new DataOutputStream(buf);
    d.writeByte(type);
    d.writeInt(4 + (body == null ? 0 : body.length));
    if (body != null) {
      d.write(body);
    }
    d.flush();
    return buf.toByteArray();
  }

  public static void writeString(DataOutputStream d, String s) throws IOException {
    d.write(s.getBytes(StandardCharsets.UTF_8));
    d.writeByte(0);
  }

  // ---- Wire helpers: decoding backend message bodies ------------------------

  public static Map<Character, String> parseErrorResponse(byte[] body) {
    Map<Character, String> fields = new LinkedHashMap<>();
    int i = 0;
    while (i < body.length && body[i] != 0) {
      char fieldType = (char) body[i];
      int start = ++i;
      while (i < body.length && body[i] != 0) {
        i++;
      }
      fields.put(fieldType, new String(body, start, i - start, StandardCharsets.UTF_8));
      i++;
    }
    return fields;
  }

  public static List<String> parseDataRow(byte[] body) {
    ByteBuffer bb = ByteBuffer.wrap(body);
    int numCols = bb.getShort() & 0xffff;
    List<String> columns = new ArrayList<>(numCols);
    for (int i = 0; i < numCols; i++) {
      int len = bb.getInt();
      if (len < 0) {
        columns.add(null);
        continue;
      }
      byte[] data = new byte[len];
      bb.get(data);
      columns.add(new String(data, StandardCharsets.UTF_8));
    }
    return columns;
  }

  public static String parseCommandComplete(byte[] body) {
    int len = body.length;
    while (len > 0 && body[len - 1] == 0) {
      len--;
    }
    return new String(body, 0, len, StandardCharsets.UTF_8);
  }

  // ---- Wire helpers: reading backend messages -------------------------------

  public static PgMessage readMessage(DataInputStream in) throws IOException {
    char type = (char) in.readUnsignedByte();
    int len = in.readInt(); // includes self (4 bytes) but not the type byte
    // A desynchronized stream reads a length out of whatever bytes come next; report that as a
    // read failure rather than blowing up the heap.
    if (len < 4 || len > MAX_MESSAGE_LENGTH) {
      throw new IOException("Implausible length " + len + " for a '" + type + "' message;"
          + " the response stream is out of sync");
    }
    byte[] body = new byte[len - 4];
    in.readFully(body);
    return new PgMessage(type, body);
  }

  /**
   * Reads backend messages until (and including) the first ReadyForQuery after
   * the authentication/startup phase. Handles AuthenticationOk,
   * ParameterStatus, and BackendKeyData transparently.
   */
  public static void readUntilReady(DataInputStream in) throws IOException {
    for (;;) {
      PgMessage msg = readMessage(in);
      switch (msg.type) {
        case BE_AUTHENTICATION:
          int authType = ByteBuffer.wrap(msg.body).getInt(0);
          if (authType != 0) {
            throw new IOException("Unexpected auth type: " + authType +
                " (only trust/AuthenticationOk is supported)");
          }
          break;
        case BE_PARAMETER_STATUS:
        case BE_BACKEND_KEY_DATA:
          break;
        case BE_READY_FOR_QUERY:
          return;
        case BE_ERROR_RESPONSE:
          throw new IOException("ErrorResponse during startup: " +
              new String(msg.body, StandardCharsets.UTF_8));
        default:
          LOG.info("Skipping unexpected startup message: " + msg);
          break;
      }
    }
  }
}
