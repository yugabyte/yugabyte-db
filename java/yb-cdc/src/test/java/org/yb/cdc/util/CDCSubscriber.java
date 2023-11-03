// Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//

package org.yb.cdc.util;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.cdc.CdcService;
import org.yb.client.*;
import org.yb.master.MasterDdlOuterClass;
import org.yb.master.MasterReplicationOuterClass;

import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.Objects;

import static org.yb.AssertionWrappers.fail;

public class CDCSubscriber {
  private final static Logger LOG = LoggerFactory.getLogger(CDCSubscriber.class);

  private String namespaceName = "yugabyte";
  private String tableName;
  private String masterAddrs;

  private String FORMAT;

  private String dbStreamId;
  private YBTable table;
  private String tableId;

  private YBClient syncClient;

  private String tabletId;

  private Checkpoint checkpoint;

  private boolean needSchemaInfo = false;

  /**
   * This is the default number of tablets as specified in AsyncYBClient
   * @see AsyncYBClient
   */
  private int numberOfTablets = 10;

  public CDCSubscriber() {
  }

  public CDCSubscriber(String masterAddrs) {
    this.masterAddrs = masterAddrs;
  }

  public CDCSubscriber(String tableName, String masterAddrs) {
    this.tableName = tableName;
    this.masterAddrs = masterAddrs;
  }

  public CDCSubscriber(String namespaceName, String tableName, String masterAddrs) {
    this.namespaceName = namespaceName;
    this.tableName = tableName;
    this.masterAddrs = masterAddrs;
  }

  /**
   * Getter function to access the YBClient for this subscriber.
   */
  public YBClient getSyncClient() {
    if (syncClient == null) {
      syncClient = createSyncClientForTest();
    }
    return syncClient;
  }

  /** Only for test purposes. */
  public void setDbStreamId(String dbStreamId) {
    this.dbStreamId = dbStreamId;
  }

  public String getDbStreamId() {
    return this.dbStreamId;
  }

  public void setTableName(String tableName) {
    this.tableName = tableName;
  }

  public boolean shouldSendSchema() {
    return needSchemaInfo;
  }

  public void setNeedSchemaInfo(boolean needSchemaInfo) {
    this.needSchemaInfo = needSchemaInfo;
  }

  public void setNumberOfTablets(int numberOfTablets) {
    this.numberOfTablets = numberOfTablets;
  }

  public String getTabletId() {
    return this.tabletId;
  }

  /**
   * This is used to set the format of the record to be streamed via CDC.
   *
   * @param recordFormat Format of the record to be streamed ("json"/"proto")
   */
  private void setFormat (String recordFormat) {
    FORMAT = recordFormat;
  }

  /**
   * Creates and returns a YBClient with the specified parameters.
   *
   * @see AsyncYBClient
   * @see YBClient
   */
  private YBClient createSyncClientForTest() {
    if (syncClient != null) {
      return syncClient;
    }

    long ADMIN_OPERATION_TIMEOUT = 30000;
    long SOCKET_READ_TIMEOUT = 30000;
    long OPERATION_TIMEOUT = 30000;

    if (masterAddrs == null) {
      masterAddrs = "127.0.0.1:7100";
    }

    LOG.info(String.format("Creating new YBClient with master address: %s", masterAddrs));

    AsyncYBClient asyncClient = new AsyncYBClient.AsyncYBClientBuilder(masterAddrs)
      .defaultAdminOperationTimeoutMs(ADMIN_OPERATION_TIMEOUT)
      .defaultOperationTimeoutMs(OPERATION_TIMEOUT)
      .defaultSocketReadTimeoutMs(SOCKET_READ_TIMEOUT)
      .numTablets(numberOfTablets)
      .build();

    return new YBClient(asyncClient);
  }

  /**
   * This function uses the created YBClient to get the tableID of the provided table name.
   *
   * @see YBClient
   * @see ListTablesResponse
   * @return TableId of provided table name
   */
  public String getTableId() throws Exception {
    if (syncClient == null) {
      syncClient = createSyncClientForTest();
    }

    if (tableId != null) {
      return tableId;
    }

    ListTablesResponse tablesResp = syncClient.getTablesList();

    String tId = "";
    String TABLE_NAME = (tableName == null || tableName.isEmpty()) ? "test" : tableName;
    String SCHEMA = namespaceName;
    for (MasterDdlOuterClass.ListTablesResponsePB.TableInfo tableInfo :
            tablesResp.getTableInfoList()) {
      if (tableInfo.getName().equals(TABLE_NAME) &&
        tableInfo.getNamespace().getName().equals(SCHEMA)) {
        tId = tableInfo.getId().toStringUtf8();
      }
    }
    if (tId == null) {
      LOG.error(String.format("Could not find a table with name %s.%s", SCHEMA, TABLE_NAME));
      System.exit(0);
    }
    return tId;
  }

  /**
   * This function uses the created YBClient and returns the table pertaining to the corresponding
   * table ID.
   *
   * @see YBTable
   * @return table of type YBTable corresponding to the provided tableId
   */
  public YBTable getTable() throws Exception {
    if (table != null) {
      return table;
    }

    if (tableId ==  null) {
      tableId = getTableId();
    }

     return syncClient.openTableByUUID(tableId);
  }

  /**
   * This function is the starting point to directly call the getChangesResponse() in order to
   * get the records streamed across CDC. It creates a YBClient first, then gets the tableId and
   * then the exact table corresponding to that tableId.
   *
   */
  private void createStreamUtil(String cpType, String recordType) throws Exception {
    syncClient = createSyncClientForTest();
    tableId = getTableId();
    table = getTable();

    String checkpointingType;

    if (cpType == null || cpType.isEmpty()) {
      checkpointingType = "IMPLICIT";
    } else {
      checkpointingType = cpType;
    }

    LOG.debug("Checkpointing type is: " + checkpointingType);
    if (dbStreamId == null || dbStreamId.isEmpty()) {
      LOG.debug("Creating a new CDC DB stream");
      dbStreamId = syncClient.createCDCStream(table, namespaceName, FORMAT,
                                              checkpointingType, recordType).getStreamId();

      LOG.debug(String.format("Created a new DB stream id: %s", dbStreamId));
    } else {
      LOG.debug("Using an old cached DB stream id");
    }

    setCheckpoint(0, 0, true);
    LOG.info("DB Stream id: " + dbStreamId);
  }

  private void createStreamUtil(String cpType) throws Exception {
    createStreamUtil(cpType, "");
  }

  /**
   * This function is used to parse the GetChangesResponse in case of Proto records
   * and add it to the provided List in order for further processing.
   *
   * @param response The GetChangesResponse we get on calling getChangesCDCSDK
   * @param records a List to add the records to after parsing it from response
   * @see GetChangesResponse
   * @see CdcService.CDCSDKProtoRecordPB
   * @see List
   */
  private void addProtoRecords(GetChangesResponse response,
                               List<CdcService.CDCSDKProtoRecordPB> records) {
    for (CdcService.CDCSDKProtoRecordPB record : response.getResp().getCdcSdkProtoRecordsList()) {
      try {
        records.add(record);
      } catch (Exception e) {
        LOG.error("Exception caught while adding records, cannot add records further", e);
        e.printStackTrace();
        fail();
      }
    }
  }

  /**
   * This function is an overloaded function which creates a CDC stream on a table
   * (in our case, test) in order to stream the data via that streamId. <br><br>
   *
   * If no record format is specified then this takes the
   * <strong>default record format as "proto"</strong> and if a record format is specified then
   * this sets the record format to that one
   *
   * @throws Exception if unable to create a stream
   */
  public void createStream() throws Exception {
    setFormat("proto");
    createStreamUtil("");
  }

  /**
   * Overloaded version of the createStream() with a parameter to specify the record format.
   * @param recordFormat Format of the record to be streamed (json/proto)
   * @throws Exception when unable to create a stream
   */
  public void createStream(String recordFormat, String recordType) throws Exception {
    recordFormat = recordFormat.toLowerCase(Locale.ROOT);
    if (!Objects.equals(recordFormat, "proto") && !Objects.equals(recordFormat, "json")) {
      LOG.error("Invalid format specified, please specify one from JSON or PROTO");
      throw new RuntimeException("Invalid format specified, specify one from JSON or PROTO");
    }
    setFormat(recordFormat);
    createStreamUtil("", recordType);
  }

  public void createStream(String recordFormat) throws Exception {
    createStream(recordFormat, "");
  }

  /**
   * Overloaded version of the createStream() with a parameter to specify the record format
   * and checkpoint type.
   * @param recordFormat Format of the record to be streamed (json/proto)
   * @param checkpointingType Type of checkpointing for the stream to be created (IMPLICIT/EXPLICIT)
   * @param recordType Type of the record, (CHANGE/ALL)
   * @throws Exception when unable to create a stream
   */
  public void createStream(String recordFormat, String checkpointingType,
                           String recordType) throws Exception {
    recordFormat = recordFormat.toLowerCase(Locale.ROOT);
    checkpointingType = checkpointingType.toUpperCase(Locale.ROOT);

    if (!Objects.equals(recordFormat, "proto") && !Objects.equals(recordFormat, "json")) {
      LOG.error("Invalid format specified, please specify one from JSON or PROTO");
      throw new RuntimeException("Invalid format specified, specify one from JSON or PROTO");
    }

    if(!Objects.equals(checkpointingType, "IMPLICIT") &&
      !Objects.equals(checkpointingType, "EXPLICIT")) {
      LOG.error("Invalid checkpointing type specified");
      throw new RuntimeException("Invalid checkpointing type, " +
        "specify one from IMPLICIT or EXPLICIT");
    }

    setFormat(recordFormat);
    createStreamUtil(checkpointingType);
  }

  /**
   * Just a getter function to access the GetDBStreamInfoResponse after we have the DB Stream ID
   * with us.
   *
   * @throws Exception when unable to access the DBStreamInfo
   * @return GetDBStreamInfoResponse
   * @see YBClient
   */
  public GetDBStreamInfoResponse getDBStreamInfo() throws Exception {
    if (syncClient == null) {
      return null;
    }

    return syncClient.getDBStreamInfo(dbStreamId);
  }

  /**
   * This function uses the YBClient and retrieves the current checkpoint for a particular table.
   *
   * @throws Exception when unable to access the checkpoint
   * @return GetCheckpointResponse
   * @see YBClient
   * @see GetCheckpointResponse
   * @see GetDBStreamInfoResponse
   */
  public GetCheckpointResponse getCheckpoint() throws Exception {
    if (syncClient == null) {
      return null;
    }

    GetDBStreamInfoResponse resp = syncClient.getDBStreamInfo(dbStreamId);

    String streamId = "";
    for (MasterReplicationOuterClass.GetCDCDBStreamInfoResponsePB.TableInfo tableInfo :
            resp.getTableInfoList()) {
      streamId = tableInfo.getStreamId().toStringUtf8();
      LOG.debug("Table StreamID: " + streamId);
    }
    if (streamId.isEmpty()) {
      return null;
    }

    if (tabletId == null) {
      List<LocatedTablet> locatedTablets = table.getTabletsLocations(30000);
      for (LocatedTablet tablet : locatedTablets) {
        LOG.debug("Tablet ID for getting checkpoint: " + new String(tablet.getTabletId()));
        this.tabletId = new String(tablet.getTabletId());
        return syncClient.getCheckpoint(table, streamId, this.tabletId);
      }
    }
    return syncClient.getCheckpoint(table, streamId, this.tabletId);
  }

  /**
   * This function is used to set the checkpoint in EXPLICIT mode using YBClient.
   *
   * @param term Checkpoint's term
   * @param index Checkpoint's index
   * @return SetCheckpointResponse
   * @throws Exception when unable to set checkpoint
   * @see YBClient
   * @see SetCheckpointResponse
   * @see GetDBStreamInfoResponse
   * @see CdcService.TableInfo
   */
  public void setCheckpoint(long term, long index, boolean initialCheckpoint) throws Exception {
    if (syncClient == null) {
      LOG.info("Cannot set checkpoint, YBClient not initialized");
      return;
    }

    GetDBStreamInfoResponse resp = syncClient.getDBStreamInfo(dbStreamId);
    String streamId = "";
    for (MasterReplicationOuterClass.GetCDCDBStreamInfoResponsePB.TableInfo tableInfo :
            resp.getTableInfoList()) {
      streamId = tableInfo.getStreamId().toStringUtf8();
      LOG.debug("Table StreamID: " + streamId);
    }
    if (streamId.isEmpty()) {
      return;
    }

    if (this.tabletId == null) {
      List<LocatedTablet> locatedTablets = table.getTabletsLocations(30000);

      for (LocatedTablet tablet : locatedTablets) {
        this.tabletId = new String(tablet.getTabletId());
        syncClient.commitCheckpoint(table, streamId, this.tabletId, term, index,
            initialCheckpoint);
        LOG.info("Set the checkpoint term : " + term + ", index: " + index);
      }
    } else {
        syncClient.commitCheckpoint(table, streamId, this.tabletId, term, index,
            initialCheckpoint);
        LOG.info("Set the checkpoint term : " + term + ", index: " + index);
    }
  }
  /**
   * This is used to create a stream to verify the snapshot feature of CDC.<br><br>
   *
   * This is a separate function currently because:
   * <ul>
   *   <li>The snapshot feature works for proto record only</li>
   *   <li>We need to set the checkpoint to a specific -1, -1 position to enable this</li>
   * </ul>
   *
   * @param records This is a List to which the streamed records would be added
   * @see org.yb.cdc.CdcService.CDCSDKProtoRecordPB
   * @see List
   */
  public void createStreamAndGetSnapshot(List records) throws Exception {
    setFormat("proto");
    syncClient = createSyncClientForTest();
    tableId = getTableId();
    table = getTable();

    if (dbStreamId == null || dbStreamId.isEmpty()) {
      LOG.debug("Creating a new CDC DB stream");
      dbStreamId = syncClient.createCDCStream(table,
        "yugabyte", FORMAT, "IMPLICIT").getStreamId();

      LOG.debug(String.format("Created a new DB stream id: %s", dbStreamId));
    } else {
      LOG.debug("Using an old cached stream id");
    }

    LOG.info("DB Stream id: " + dbStreamId);

    getSnapshotFromCDC(records);
  }

  /**
   * This function is used in conjunction with createStreamAndGetSnapshot(). It's a separate
   * function because to test the snapshot feature, the checkpoint should be set at <-1, -1>
   *
   * @param records The streamed record/snapshot records would be added in this list
   * @see GetChangesResponse
   * @throws Exception if unable to get snapshots
   */
  public void getSnapshotFromCDC(List records) throws Exception {
    Checkpoint cp = new Checkpoint(-1, -1, "".getBytes(), -1, 0);
    getResponseFromCDC(records, cp);

    // The first call would not return any values and will return a list without any records,
    // to overcome this, call the getChanges function again.
    if (records.isEmpty()) {
      getResponseFromCDC(records, getSubscriberCheckpoint());
    }
  }

  /**
   * This function calls the getChangesCDCSDK() function in order to get the response from the CDC
   * stream and adds the streamed record to the passed list.
   *
   * @param records List to store the CDC records
   * @see GetChangesResponse
   * @throws Exception if unable to get changes response from CDC
   */
  public void getResponseFromCDC(List records) throws Exception {
    Checkpoint cp = new Checkpoint(0, 0, new byte[]{}, 0, 0);
    getResponseFromCDC(records, cp);
  }

  public void getResponseFromCDC(List records, Checkpoint cp) throws Exception {
    List<LocatedTablet> locatedTablets = table.getTabletsLocations(30000);
    List<String> tabletIds = new ArrayList<>();

    for (LocatedTablet tablet : locatedTablets) {
      String tabletId = new String(tablet.getTabletId());
      LOG.debug(String.format("Polling for tablet: %s from checkpoint %s", tabletId, cp));
      this.tabletId = tabletId; // There is going to be a single tablet only.
      tabletIds.add(tabletId);
    }

    for (String tabletId : tabletIds) {
      syncClient.commitCheckpoint(table, dbStreamId, tabletId, 0, 0, true);

      GetChangesResponse changesResponse =
        syncClient.getChangesCDCSDK(
          table, dbStreamId, tabletId,
          cp.getTerm(), cp.getIndex(), cp.getKey(), cp.getWriteId(), cp.getSnapshotTime(),
          shouldSendSchema());

      if (FORMAT.equalsIgnoreCase("PROTO")) {
        // Add records in proto.
        addProtoRecords(changesResponse, records);
      }

      updateCheckpointValues(changesResponse);
    }
  }

  /**
   * Take the change response and update the cached checkpoint based on the values in the response.
   * @param changesResponse The {@link GetChangesResponse} received after a
   *        {@link YBClient#getChangesCDCSDK} request.
   * @see GetChangesResponse
   * @see Checkpoint
   */
  private void updateCheckpointValues(GetChangesResponse changesResponse) {
    checkpoint = new Checkpoint(changesResponse.getTerm(),
                                changesResponse.getIndex(),
                                changesResponse.getKey(),
                                changesResponse.getWriteId(),
                                changesResponse.getSnapshotTime());
  }

  /**
   *
   * @return the cached checkpoint in the CDCSubscriber
   * @throws NullPointerException
   * @see Checkpoint
   */
  public Checkpoint getSubscriberCheckpoint() throws NullPointerException {
    if (checkpoint == null) {
      throw new NullPointerException("Checkpoint has not been set yet");
    }
    return checkpoint;
  }
}
