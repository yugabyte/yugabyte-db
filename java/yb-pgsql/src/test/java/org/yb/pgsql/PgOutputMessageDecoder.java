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
package org.yb.pgsql;

import com.google.common.base.Objects;
import com.yugabyte.replication.LogSequenceNumber;
import java.lang.String;
import java.nio.ByteBuffer;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

import javax.annotation.Nullable;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

/*
 * Test utility to decode the message streamed by the 'pgoutput' plugin as part of the Logical
 * Replication protocol.
 *
 * The format is described at
 * https://www.postgresql.org/docs/11/protocol-logicalrep-message-formats.html
 */
public class PgOutputMessageDecoder {
  private static final Logger LOG = LoggerFactory.getLogger(PgOutputMessageDecoder.class);

  public enum PgOutputMessageType { RELATION, TYPE, BEGIN, COMMIT, INSERT, UPDATE, DELETE };

  public interface PgOutputMessage {
    PgOutputMessageType messageType();
  }

  /*
   * RELATION message
   *
   * Declared final since we have overriden equals to ignore irrelevant fields such as oid which
   * make the test brittle.
   */
  protected final static class PgOutputRelationMessage implements PgOutputMessage {
    final int oid;
    final String namespace;
    final String name;
    final char replicaIdentity;
    final List<PgOutputRelationMessageColumn> columns;

    public PgOutputRelationMessage(int oid, String namespace, String name, char replicaIdentity,
        List<PgOutputRelationMessageColumn> columns) {
      this.oid = oid;
      this.namespace = namespace;
      this.name = name;
      this.replicaIdentity = replicaIdentity;
      this.columns = columns;
    }

    public static PgOutputRelationMessage CreateForComparison(String namespace, String name,
        char replicaIdentity, List<PgOutputRelationMessageColumn> columns) {
      return new PgOutputRelationMessage(0, namespace, name, replicaIdentity, columns);
    }

    @Override
    public PgOutputMessageType messageType() {
      return PgOutputMessageType.RELATION;
    }

    @Override
    public boolean equals(Object other) {
      if (this == other)
        return true;

      if (!(other instanceof PgOutputRelationMessage)) {
        return false;
      }

      PgOutputRelationMessage otherMessage = (PgOutputRelationMessage) other;
      return this.namespace.equals(otherMessage.namespace)
          && this.name.equals(otherMessage.name)
          && this.replicaIdentity == otherMessage.replicaIdentity
          && this.columns.equals(otherMessage.columns);
    }

    @Override
    public int hashCode() {
      return Objects.hashCode(namespace, name, replicaIdentity, columns);
    }

    @Override
    public String toString() {
      return String.format(
          "RELATION: (name = %s, namespace = %s, replica_identity = %s, columns = %s)", name,
          namespace, replicaIdentity, Arrays.toString(columns.toArray()));
    }
  }

  /*
   * A single column of a Relation.
   */
  protected final static class PgOutputRelationMessageColumn {
    final byte flag;
    final String name;
    final int dataType;
    final int atttypmod;
    // We don't want to compare data type (oid) in the case of user defined types. This is because
    // comparing OIDs for user defined types will make the test brittle and it depends on the build.
    final boolean compareDataType;

    public PgOutputRelationMessageColumn(
        byte flag, String name, int dataType, int atttypmod, boolean compareDataType) {
      this.flag = flag;
      this.name = name;
      this.dataType = dataType;
      this.atttypmod = atttypmod;
      this.compareDataType = compareDataType;
    }

    public PgOutputRelationMessageColumn(byte flag, String name) {
      this.flag = flag;
      this.name = name;
      // This constructor is used for columns having dynamic types. Since their type OIDs
      // are dynamic they will not be compared during the tests.
      this.dataType = 0;
      this.atttypmod = 0;
      this.compareDataType = false;
    }

    public static PgOutputRelationMessageColumn CreateForComparison(String name, int dataType) {
      return new PgOutputRelationMessageColumn(
          (byte) 0, name, dataType, 0, /* compareDataType */ true);
    }

    public static PgOutputRelationMessageColumn CreateForComparison(String name) {
      return new PgOutputRelationMessageColumn((byte) 0, name);
    }

    @Override
    public boolean equals(Object other) {
      if (this == other)
        return true;

      if (!(other instanceof PgOutputRelationMessageColumn)) {
        return false;
      }

      PgOutputRelationMessageColumn otherColumn = (PgOutputRelationMessageColumn) other;
      // Skip data type comparison if required.
      boolean dataTypeComparison = (this.compareDataType && otherColumn.compareDataType)
          ? this.dataType == otherColumn.dataType
          : true;
      return this.name.equals(otherColumn.name) && dataTypeComparison;
    }

    @Override
    public int hashCode() {
      return Objects.hashCode(name, dataType);
    }

    @Override
    public String toString() {
      return String.format("(name = %s, data_type = %s)", name, dataType);
    }
  }

  /*
   * TYPE message
   *
   * Declared final since we have overriden equals to ignore irrelevant fields such as oid which
   * make the test brittle.
   */
  protected final static class PgOutputTypeMessage implements PgOutputMessage {
    final int oid;
    final String namespace;
    final String name;

    public PgOutputTypeMessage(int oid, String namespace, String name) {
      this.oid = oid;
      this.namespace = namespace;
      this.name = name;
    }

    public static PgOutputTypeMessage CreateForComparison(String namespace, String name) {
      return new PgOutputTypeMessage(0, namespace, name);
    }

    @Override
    public PgOutputMessageType messageType() {
      return PgOutputMessageType.TYPE;
    }

    @Override
    public boolean equals(Object other) {
      if (this == other)
        return true;

      if (!(other instanceof PgOutputTypeMessage)) {
        return false;
      }

      PgOutputTypeMessage otherMessage = (PgOutputTypeMessage) other;
      return this.namespace.equals(otherMessage.namespace) && this.name.equals(otherMessage.name);
    }

    @Override
    public int hashCode() {
      return Objects.hashCode(namespace, name);
    }

    @Override
    public String toString() {
      return String.format("TYPE: (name = %s, namespace = %s)", name, namespace);
    }
  }

  /*
   * BEGIN message
   *
   * Declared final since we have overriden equals to ignore irrelevant fields such as commitTime
   * which changes from execution to execution.
   */
  protected final static class PgOutputBeginMessage implements PgOutputMessage {
    final LogSequenceNumber finalLSN;
    final Long commitTime;
    final int transactionId;

    public PgOutputBeginMessage(
        LogSequenceNumber finalLSN, Long commitTime, int transactionId) {
      this.finalLSN = finalLSN;
      this.commitTime = commitTime;
      this.transactionId = transactionId;
    }

    public static PgOutputBeginMessage CreateForComparison(
        LogSequenceNumber finalLSN, int transactionId) {
      return new PgOutputBeginMessage(finalLSN, 0L, transactionId);
    }

    @Override
    public PgOutputMessageType messageType() {
      return PgOutputMessageType.BEGIN;
    }

    @Override
    public boolean equals(Object other) {
      if (this == other)
        return true;

      if (!(other instanceof PgOutputBeginMessage)) {
        return false;
      }

      PgOutputBeginMessage otherMessage = (PgOutputBeginMessage) other;
      return this.finalLSN.equals(otherMessage.finalLSN)
          && this.transactionId == otherMessage.transactionId;
    }

    @Override
    public int hashCode() {
      return Objects.hashCode(finalLSN, transactionId);
    }

    @Override
    public String toString() {
      return String.format("BEGIN: (lsn = %s, xid = %s)", finalLSN, transactionId);
    }
  }

  /*
   * COMMIT message
   *
   * Declared final since we have overriden equals to ignore irrelevant fields such as commitTime
   * which changes from execution to execution.
   */
  protected final static class PgOutputCommitMessage implements PgOutputMessage {
    final byte flag;
    final Long commitTime;
    final LogSequenceNumber commitLSN;
    final LogSequenceNumber endLSN;

    public PgOutputCommitMessage(
        byte flag, Long commitTime, LogSequenceNumber commitLSN, LogSequenceNumber endLSN) {
      this.flag = flag;
      this.commitTime = commitTime;
      this.commitLSN = commitLSN;
      this.endLSN = endLSN;
    }

    public static PgOutputCommitMessage CreateForComparison(
        LogSequenceNumber commitLSN, LogSequenceNumber endLSN) {
      return new PgOutputCommitMessage((byte)0, 0L, commitLSN, endLSN);
    }

    @Override
    public PgOutputMessageType messageType() {
      return PgOutputMessageType.COMMIT;
    }

    @Override
    public boolean equals(Object other) {
      if (this == other)
        return true;

      if (!(other instanceof PgOutputCommitMessage)) {
        return false;
      }

      PgOutputCommitMessage otherMessage = (PgOutputCommitMessage) other;
      return this.commitLSN.equals(otherMessage.commitLSN)
          && this.endLSN.equals(otherMessage.endLSN);
    }

    @Override
    public int hashCode() {
      return Objects.hashCode(commitLSN, endLSN);
    }

    @Override
    public String toString() {
      return String.format(
          "COMMIT: (commit_lsn = %s, end_lsn = %s, flag = %s)", commitLSN, endLSN, flag);
    }
  }

  /*
   * INSERT message
   *
   * Declared final since we have overriden equals to ignore irrelevant fields such as oid
   * which make the test brittle.
   */
  protected final static class PgOutputInsertMessage implements PgOutputMessage {
    final int oid;
    final PgOutputMessageTuple tuple;

    public PgOutputInsertMessage(int oid, PgOutputMessageTuple tuple) {
      this.oid = oid;
      this.tuple = tuple;
    }

    public static PgOutputInsertMessage CreateForComparison(PgOutputMessageTuple tuple) {
      return new PgOutputInsertMessage(0, tuple);
    }

    @Override
    public PgOutputMessageType messageType() {
      return PgOutputMessageType.INSERT;
    }

    @Override
    public boolean equals(Object other) {
      if (this == other)
        return true;

      if (!(other instanceof PgOutputInsertMessage)) {
        return false;
      }

      PgOutputInsertMessage otherMessage = (PgOutputInsertMessage) other;
      return this.tuple.equals(otherMessage.tuple);
    }

    @Override
    public int hashCode() {
      return Objects.hashCode(tuple);
    }

    @Override
    public String toString() {
      return String.format("INSERT: (tuple = %s)", tuple);
    }
  }

  /*
   * The tuple describing a row of data.
   */
  protected final static class PgOutputMessageTuple {
    final short numColumns;
    final List<PgOutputMessageTupleColumn> columns;

    public PgOutputMessageTuple(short numColumns, List<PgOutputMessageTupleColumn> columns) {
      this.numColumns = numColumns;
      this.columns = columns;
    }

    @Override
    public boolean equals(Object other) {
      if (this == other)
        return true;

      if (!(other instanceof PgOutputMessageTuple)) {
        return false;
      }

      PgOutputMessageTuple otherTuple = (PgOutputMessageTuple) other;
      return this.numColumns == otherTuple.numColumns && this.columns.equals(otherTuple.columns);
    }

    @Override
    public int hashCode() {
      return Objects.hashCode(numColumns, columns);
    }

    @Override
    public String toString() {
      return String.format(
          "(num_columns = %s, columns = %s)", numColumns, Arrays.toString(columns.toArray()));
    }
  }

  /*
   * Data of a single column present in a tuple.
   */
  protected interface PgOutputMessageTupleColumn {
  }

  protected final static class PgOutputMessageTupleColumnNull
      implements PgOutputMessageTupleColumn {
    @Override
    public boolean equals(Object other) {
      return other instanceof PgOutputMessageTupleColumnNull;
    }

    @Override
    public int hashCode() {
      return Objects.hashCode("NULL");
    }

    @Override
    public String toString() {
      return "NULL";
    }
  }

  protected final static class PgOutputMessageTupleColumnToasted
      implements PgOutputMessageTupleColumn {
    @Override
    public boolean equals(Object other) {
      return other instanceof PgOutputMessageTupleColumnToasted;
    }

    @Override
    public int hashCode() {
      return Objects.hashCode("TOASTED");
    }

    @Override
    public String toString() {
      return "TOASTED";
    }
  }

  protected final static class PgOutputMessageTupleColumnValue
      implements PgOutputMessageTupleColumn {
    final String textValue;

    public PgOutputMessageTupleColumnValue(String textValue) {
      this.textValue = textValue;
    }

    @Override
    public boolean equals(Object other) {
      if (this == other)
        return true;

      if (!(other instanceof PgOutputMessageTupleColumnValue)) {
        return false;
      }

      PgOutputMessageTupleColumnValue otherValue = (PgOutputMessageTupleColumnValue) other;
      return this.textValue.equals(otherValue.textValue);
    }

    @Override
    public int hashCode() {
      return Objects.hashCode(textValue);
    }

    @Override
    public String toString() {
      return textValue;
    }
  }

  /*
   * UPDATE message
   */
  protected static class PgOutputUpdateMessage implements PgOutputMessage {
    final int oid;
    @Nullable final PgOutputMessageTuple old_tuple;
    final PgOutputMessageTuple new_tuple;

    public PgOutputUpdateMessage(
        int oid, @Nullable PgOutputMessageTuple old_tuple, PgOutputMessageTuple new_tuple) {
      this.oid = oid;
      this.old_tuple = old_tuple;
      this.new_tuple = new_tuple;
    }

    public static PgOutputUpdateMessage CreateForComparison(
        @Nullable PgOutputMessageTuple old_tuple, PgOutputMessageTuple new_tuple) {
      return new PgOutputUpdateMessage(0, old_tuple, new_tuple);
    }

    @Override
    public PgOutputMessageType messageType() {
      return PgOutputMessageType.UPDATE;
    }

    @Override
    public boolean equals(Object other) {
      if (this == other)
        return true;

      if (other == null || this.getClass() != other.getClass())
        return false;

      PgOutputUpdateMessage otherMessage = (PgOutputUpdateMessage) other;
      return ((this.old_tuple == null) ? otherMessage.old_tuple == null
                                       : this.old_tuple.equals(otherMessage.old_tuple))
          && this.new_tuple.equals(otherMessage.new_tuple);
    }

    @Override
    public String toString() {
      String old_tuple_string = (this.old_tuple != null) ? old_tuple.toString() : "NULL";
      // We only output the fields that matter for comparison so that it is easier to do a
      // diff-check while debugging tests.
      return String.format("UPDATE: (old_tuple = %s, new_tuple = %s)", old_tuple_string, new_tuple);
    }
  }

  /*
   * DELETE message
   */
  protected static class PgOutputDeleteMessage implements PgOutputMessage {
    final int oid;
    final boolean hasKey;
    final PgOutputMessageTuple oldTuple;

    public PgOutputDeleteMessage(int oid, boolean hasKey, PgOutputMessageTuple oldTuple) {
      this.oid = oid;
      this.hasKey = hasKey;
      this.oldTuple = oldTuple;
    }

    public static PgOutputDeleteMessage CreateForComparison(
        boolean hasKey, PgOutputMessageTuple oldTuple) {
      return new PgOutputDeleteMessage(0, hasKey, oldTuple);
    }

    @Override
    public PgOutputMessageType messageType() {
      return PgOutputMessageType.DELETE;
    }

    @Override
    public boolean equals(Object other) {
      if (this == other)
        return true;

      if (other == null || this.getClass() != other.getClass())
        return false;

      PgOutputDeleteMessage otherMessage = (PgOutputDeleteMessage) other;
      return this.oldTuple.equals(otherMessage.oldTuple);
    }

    @Override
    public String toString() {
      String oldTupleString = oldTuple.toString();
      // We only output the fields that matter for comparison so that it is easier to do a
      // diff-check while debugging tests.
      return String.format("DELETE: (oldTuple = %s, hasKey = %b)", oldTupleString, hasKey);
    }
  }

  /*
   * Decode the data passed as bytes into a PgOutputMessage.
   */
  public static PgOutputMessage DecodeBytes(ByteBuffer buf) throws Exception {
    final byte[] source = buf.array();
    final ByteBuffer buffer =
        ByteBuffer.wrap(Arrays.copyOfRange(source, buf.arrayOffset(), source.length));

    byte cmd = buffer.get();
    switch (cmd) {
      case 'R': // RELATION
        int oid = buffer.getInt();
        String namespace = decodeString(buffer);
        String name = decodeString(buffer);
        char replicaIdent = (char) buffer.get();
        short numAttrs = buffer.getShort();

        List<PgOutputRelationMessageColumn> columns = new ArrayList<>();
        for (int i = 0; i < numAttrs; i++) {
          byte flag = buffer.get();
          String attrName = decodeString(buffer);
          int attrId = buffer.getInt();
          int attrMode = buffer.getInt();
          columns.add(new PgOutputRelationMessageColumn(
              flag, attrName, attrId, attrMode, /* compareDataType */ true));
        }

        return new PgOutputRelationMessage(oid, namespace, name, replicaIdent, columns);

      case 'B': // BEGIN
        LogSequenceNumber finalLSN = LogSequenceNumber.valueOf(buffer.getLong());
        Long commitTime = buffer.getLong();
        int transactionId = buffer.getInt();
        PgOutputBeginMessage beginMessage =
            new PgOutputBeginMessage(finalLSN, commitTime, transactionId);
        return beginMessage;

      case 'C': // COMMIT
        byte unusedFlag = buffer.get();
        LogSequenceNumber commitLSN = LogSequenceNumber.valueOf(buffer.getLong());
        LogSequenceNumber endLSN = LogSequenceNumber.valueOf(buffer.getLong());
        commitTime = buffer.getLong();
        PgOutputCommitMessage commitMessage =
            new PgOutputCommitMessage(unusedFlag, commitTime, commitLSN, endLSN);
        return commitMessage;

      case 'I': // INSERT
        oid = buffer.getInt();
        expectValue('N', (char)buffer.get());
        PgOutputMessageTuple tuple = decodePgOutputMessageTuple(buffer);
        PgOutputInsertMessage insertMessage = new PgOutputInsertMessage(oid, tuple);
        return insertMessage;

      case 'U': // UPDATE
        oid = buffer.getInt();
        char oldOrNew = (char)buffer.get();

        // 'K' or 'O' represents old tuple while 'N' represents new tuple
        PgOutputMessageTuple old_tuple = null;
        if (oldOrNew == 'K' || oldOrNew == 'O') {
          old_tuple = decodePgOutputMessageTuple(buffer);
          buffer.get(); // Always 'N'
        }

        PgOutputMessageTuple new_tuple = decodePgOutputMessageTuple(buffer);
        PgOutputUpdateMessage updateMessage = new PgOutputUpdateMessage(oid, old_tuple, new_tuple);
        return updateMessage;

      case 'D': // DELETE
        oid = buffer.getInt();
        char tupleType = (char) buffer.get();
        old_tuple = decodePgOutputMessageTuple(buffer);
        // 'K' represents key while 'O' represents complete old tuple.
        PgOutputDeleteMessage deleteMessage =
            new PgOutputDeleteMessage(oid, tupleType == 'K', old_tuple);
        return deleteMessage;

      case 'Y': // TYPE
        oid = buffer.getInt();
        namespace = decodeString(buffer);
        name = decodeString(buffer);
        return new PgOutputTypeMessage(oid, namespace, name);
    }

    LOG.info(String.format("Received unknown response %c, returning null", cmd));
    return null;
  }

  private static PgOutputMessageTuple decodePgOutputMessageTuple(ByteBuffer buffer) {
    short numColumns = buffer.getShort();
    List<PgOutputMessageTupleColumn> columns = new ArrayList<PgOutputMessageTupleColumn>();

    for (int i = 0; i < numColumns; i++) {
      byte c = buffer.get();

      switch (c) {
        case 'n':
          columns.add(new PgOutputMessageTupleColumnNull());
          break;
        case 'u':
          columns.add(new PgOutputMessageTupleColumnToasted());
          break;
        case 't':
          int strLen = buffer.getInt();
          byte[] bytes = new byte[strLen];
          buffer.get(bytes, 0, strLen);
          String value = new String(bytes);
          columns.add(new PgOutputMessageTupleColumnValue(value));
          break;
      }
    }

    return new PgOutputMessageTuple(numColumns, columns);
  }

  private static String decodeString(ByteBuffer buffer) {
    StringBuffer sb = new StringBuffer();
    while (true) {
      byte c = buffer.get();
      if (c == 0) {
        break;
      }
      sb.append((char) c);
    }
    return sb.toString();
  }

  private static void expectValue(char expected, char actual) throws Exception {
    if (expected != actual) {
      throw new AssertionError(String.format("Expected: %c, Found: %c", expected, actual));
    }
  }
}
