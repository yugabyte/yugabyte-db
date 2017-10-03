// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
package org.kududb.client;

import com.google.protobuf.ByteString;
import com.google.protobuf.Message;
import com.google.protobuf.ZeroCopyLiteralByteString;

import org.kududb.ColumnSchema;
import org.kududb.Schema;
import org.kududb.Type;
import org.kududb.WireProtocol.RowOperationsPB;
import org.kududb.annotations.InterfaceAudience;
import org.kududb.annotations.InterfaceStability;
import org.kududb.tserver.Tserver;
import org.kududb.util.Pair;
import org.jboss.netty.buffer.ChannelBuffer;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.ArrayList;
import java.util.List;

/**
 * Base class for the RPCs that related to WriteRequestPB. It contains almost all the logic
 * and knows how to serialize its child classes.
 */
@InterfaceAudience.Public
@InterfaceStability.Evolving
public abstract class Operation extends KuduRpc<OperationResponse> implements KuduRpc.HasKey {

  // Number given by the session when apply()'d for the first time. Necessary to retain operations
  // in their original order even after tablet lookup.
  private long sequenceNumber = -1;

  enum ChangeType {
    INSERT((byte)RowOperationsPB.Type.INSERT.getNumber()),
    UPDATE((byte)RowOperationsPB.Type.UPDATE.getNumber()),
    DELETE((byte)RowOperationsPB.Type.DELETE.getNumber()),
    SPLIT_ROWS((byte)RowOperationsPB.Type.SPLIT_ROW.getNumber());

    ChangeType(byte encodedByte) {
      this.encodedByte = encodedByte;
    }

    byte toEncodedByte() {
      return encodedByte;
    }

    /** The byte used to encode this in a RowOperationsPB */
    private byte encodedByte;
  }

  static final String METHOD = "Write";

  private final PartialRow row;

  /**
   * Package-private constructor. Subclasses need to be instantiated via AsyncKuduSession
   * @param table table with the schema to use for this operation
   */
  Operation(KuduTable table) {
    super(table);
    this.row = table.getSchema().newPartialRow();
  }

  /**
   * Classes extending Operation need to have a specific ChangeType
   * @return Operation's ChangeType
   */
  abstract ChangeType getChangeType();


  /**
   * Sets the sequence number used when batching operations. Should only be called once.
   * @param sequenceNumber a new sequence number
   */
  void setSequenceNumber(long sequenceNumber) {
    assert (this.sequenceNumber == -1);
    this.sequenceNumber = sequenceNumber;
  }

  /**
   * Returns the sequence number given to this operation.
   * @return a long representing the sequence number given to this operation after it was applied,
   * can be -1 if it wasn't set
   */
  long getSequenceNumber() {
    return this.sequenceNumber;
  }

  @Override
  String serviceName() { return TABLET_SERVER_SERVICE_NAME; }

  @Override
  String method() {
    return METHOD;
  }

  @Override
  ChannelBuffer serialize(Message header) {
    final Tserver.WriteRequestPB.Builder builder = createAndFillWriteRequestPB(this);
    builder.setTabletId(ZeroCopyLiteralByteString.wrap(getTablet().getTabletIdAsBytes()));
    builder.setExternalConsistencyMode(this.externalConsistencyMode.pbVersion());
    if (this.propagatedTimestamp != AsyncKuduClient.NO_TIMESTAMP) {
      builder.setPropagatedTimestamp(this.propagatedTimestamp);
    }
    return toChannelBuffer(header, builder.build());
  }

  @Override
  Pair<OperationResponse, Object> deserialize(CallResponse callResponse,
                                              String tsUUID) throws Exception {
    Tserver.WriteResponsePB.Builder builder = Tserver.WriteResponsePB.newBuilder();
    readProtobuf(callResponse.getPBMessage(), builder);
    Tserver.WriteResponsePB.PerRowErrorPB error = null;
    if (builder.getPerRowErrorsCount() != 0) {
      error = builder.getPerRowErrors(0);
    }
    OperationResponse response = new OperationResponse(deadlineTracker.getElapsedMillis(), tsUUID,
        builder.getTimestamp(), this, error);
    return new Pair<OperationResponse, Object>(
        response, builder.hasError() ? builder.getError() : null);
  }

  @Override
  public byte[] partitionKey() {
    return this.getTable().getPartitionSchema().encodePartitionKey(row);
  }

  /**
   * Get the underlying row to modify.
   * @return a partial row that will be sent with this Operation
   */
  public PartialRow getRow() {
    return this.row;
  }

  @Override
  public String toString() {
    StringBuilder sb = new StringBuilder(super.toString());
    sb.append(" row_key=");
    sb.append(row.stringifyRowKey());
    return sb.toString();
  }

  /**
   * Helper method that puts a list of Operations together into a WriteRequestPB.
   * @param operations The list of ops to put together in a WriteRequestPB
   * @return A fully constructed WriteRequestPB containing the passed rows, or
   *         null if no rows were passed.
   */
  static Tserver.WriteRequestPB.Builder createAndFillWriteRequestPB(Operation... operations) {
    if (operations == null || operations.length == 0) return null;
    Schema schema = operations[0].table.getSchema();
    RowOperationsPB rowOps = new OperationsEncoder().encodeOperations(operations);
    if (rowOps == null) return null;

    Tserver.WriteRequestPB.Builder requestBuilder = Tserver.WriteRequestPB.newBuilder();
    requestBuilder.setSchema(ProtobufHelper.schemaToPb(schema));
    requestBuilder.setRowOperations(rowOps);
    return requestBuilder;
  }

  static class OperationsEncoder {
    private Schema schema;
    private ByteBuffer rows;
    // We're filling this list as we go through the operations in encodeRow() and at the same time
    // compute the total size, which will be used to right-size the array in toPB().
    private List<ByteBuffer> indirect;
    private long indirectWrittenBytes;

    /**
     * Initializes the state of the encoder based on the schema and number of operations to encode.
     *
     * @param schema the schema of the table which the operations belong to.
     * @param numOperations the number of operations.
     */
    private void init(Schema schema, int numOperations) {
      this.schema = schema;

      // Set up the encoded data.
      // Estimate a maximum size for the data. This is conservative, but avoids
      // having to loop through all the operations twice.
      final int columnBitSetSize = Bytes.getBitSetSize(schema.getColumnCount());
      int sizePerRow = 1 /* for the op type */ + schema.getRowSize() + columnBitSetSize;
      if (schema.hasNullableColumns()) {
        // nullsBitSet is the same size as the columnBitSet
        sizePerRow += columnBitSetSize;
      }

      // TODO: would be more efficient to use a buffer which "chains" smaller allocations
      // instead of a doubling buffer like BAOS.
      this.rows = ByteBuffer.allocate(sizePerRow * numOperations)
                            .order(ByteOrder.LITTLE_ENDIAN);
      this.indirect = new ArrayList<>(schema.getVarLengthColumnCount() * numOperations);
    }

    /**
     * Builds the row operations protobuf message with encoded operations.
     * @return the row operations protobuf message.
     */
    private RowOperationsPB toPB() {
      RowOperationsPB.Builder rowOpsBuilder = RowOperationsPB.newBuilder();

      // TODO: we could implement a ZeroCopy approach here by subclassing LiteralByteString.
      // We have ZeroCopyLiteralByteString, but that only supports an entire array. Here
      // we've only partially filled in rows.array(), so we have to make the extra copy.
      rows.limit(rows.position());
      rows.flip();
      rowOpsBuilder.setRows(ByteString.copyFrom(rows));
      if (indirect.size() > 0) {
        // TODO: same as above, we could avoid a copy here by using an implementation that allows
        // zero-copy on a slice of an array.
        byte[] indirectData = new byte[(int)indirectWrittenBytes];
        int offset = 0;
        for (ByteBuffer bb : indirect) {
          int bbSize = bb.remaining();
          bb.get(indirectData, offset, bbSize);
          offset += bbSize;
        }
        rowOpsBuilder.setIndirectData(ZeroCopyLiteralByteString.wrap(indirectData));
      }
      return rowOpsBuilder.build();
    }

    private void encodeRow(PartialRow row, ChangeType type) {
      rows.put(type.toEncodedByte());
      rows.put(Bytes.fromBitSet(row.getColumnsBitSet(), schema.getColumnCount()));
      if (schema.hasNullableColumns()) {
        rows.put(Bytes.fromBitSet(row.getNullsBitSet(), schema.getColumnCount()));
      }
      int colIdx = 0;
      byte[] rowData = row.getRowAlloc();
      int currentRowOffset = 0;
      for (ColumnSchema col : row.getSchema().getColumns()) {
        // Keys should always be specified, maybe check?
        if (row.isSet(colIdx) && !row.isSetToNull(colIdx)) {
          if (col.getType() == Type.STRING || col.getType() == Type.BINARY) {
            ByteBuffer varLengthData = row.getVarLengthData().get(colIdx);
            varLengthData.reset();
            rows.putLong(indirectWrittenBytes);
            int bbSize = varLengthData.remaining();
            rows.putLong(bbSize);
            indirect.add(varLengthData);
            indirectWrittenBytes += bbSize;
          } else {
            // This is for cols other than strings
            rows.put(rowData, currentRowOffset, col.getType().getSize());
          }
        }
        currentRowOffset += col.getType().getSize();
        colIdx++;
      }
    }

    public RowOperationsPB encodeOperations(Operation... operations) {
      if (operations == null || operations.length == 0) return null;
      init(operations[0].table.getSchema(), operations.length);
      for (Operation operation : operations) {
        encodeRow(operation.row, operation.getChangeType());
      }
      return toPB();
    }

    public RowOperationsPB encodeSplitRows(List<PartialRow> rows) {
      if (rows == null || rows.isEmpty()) return null;
      init(rows.get(0).getSchema(), rows.size());
      for (PartialRow row : rows) {
        encodeRow(row, ChangeType.SPLIT_ROWS);
      }
      return toPB();
    }
  }
}
