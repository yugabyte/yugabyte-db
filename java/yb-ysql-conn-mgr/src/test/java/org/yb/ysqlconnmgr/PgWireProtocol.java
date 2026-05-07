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

  private PgWireProtocol() {}

  // -- Wire-protocol message types (backend) ----------------------------------
  public static final char BE_AUTHENTICATION = 'R';
  public static final char BE_PARAMETER_STATUS = 'S';
  public static final char BE_BACKEND_KEY_DATA = 'K';
  public static final char BE_READY_FOR_QUERY = 'Z';
  public static final char BE_PARSE_COMPLETE = '1';
  public static final char BE_BIND_COMPLETE = '2';
  public static final char BE_DATA_ROW = 'D';
  public static final char BE_COMMAND_COMPLETE = 'C';
  public static final char BE_ERROR_RESPONSE = 'E';
  public static final char BE_COPY_IN_RESPONSE = 'G';
  public static final char BE_NOTICE_RESPONSE = 'N';
  public static final char BE_ROW_DESCRIPTION = 'T';

  // ---- Tiny record for a backend message ------------------------------------
  public static class PgMessage {
    public final char type;
    public final byte[] body;

    public PgMessage(char type, byte[] body) {
      this.type = type;
      this.body = body;
    }

    @Override
    public String toString() {
      return "PgMessage['" + type + "', len=" + body.length + "]";
    }
  }

  // ---- Wire helpers: building frontend messages -----------------------------

  public static byte[] buildStartupMessage(String user, String database) throws IOException {
    ByteArrayOutputStream buf = new ByteArrayOutputStream();
    DataOutputStream d = new DataOutputStream(buf);
    d.writeInt(0); // placeholder for length
    d.writeInt(196608); // protocol version 3.0
    writeString(d, "user");
    writeString(d, user);
    writeString(d, "database");
    writeString(d, database);
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
    ByteArrayOutputStream buf = new ByteArrayOutputStream();
    DataOutputStream d = new DataOutputStream(buf);
    d.writeByte('B');
    d.writeInt(0); // placeholder
    d.writeByte(0); // unnamed portal
    d.writeByte(0); // unnamed statement
    d.writeShort(0); // num format codes
    d.writeShort(0); // num parameters
    d.writeShort(0); // num result format codes
    d.flush();
    byte[] msg = buf.toByteArray();
    ByteBuffer.wrap(msg).putInt(1, msg.length - 1);
    return msg;
  }

  public static byte[] buildBind(String stmtName, String[] textParams) throws IOException {
    ByteArrayOutputStream buf = new ByteArrayOutputStream();
    DataOutputStream d = new DataOutputStream(buf);
    d.writeByte('B');
    d.writeInt(0); // placeholder
    d.writeByte(0); // unnamed portal
    writeString(d, stmtName);
    d.writeShort(0); // num format codes (all text)
    d.writeShort(textParams.length);
    for (String param : textParams) {
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
    ByteArrayOutputStream buf = new ByteArrayOutputStream();
    DataOutputStream d = new DataOutputStream(buf);
    d.writeByte('E');
    d.writeInt(0); // placeholder
    d.writeByte(0); // unnamed portal
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

  public static void writeString(DataOutputStream d, String s) throws IOException {
    d.write(s.getBytes(StandardCharsets.UTF_8));
    d.writeByte(0);
  }

  // ---- Wire helpers: reading backend messages -------------------------------

  public static PgMessage readMessage(DataInputStream in) throws IOException {
    char type = (char) in.readUnsignedByte();
    int len = in.readInt(); // includes self (4 bytes) but not the type byte
    byte[] body = new byte[len - 4];
    in.readFully(body);
    return new PgMessage(type, body);
  }

  /**
   * Reads the next backend message, silently skipping any NoticeResponse
   * messages. NoticeResponse is an asynchronous informational message that
   * can appear at any point in the stream.
   */
  public static PgMessage readMessageSkipNotice(DataInputStream in) throws IOException {
    for (;;) {
      PgMessage msg = readMessage(in);
      if (msg.type != BE_NOTICE_RESPONSE)
        return msg;
      LOG.info("Skipping NoticeResponse: " + msg);
    }
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
