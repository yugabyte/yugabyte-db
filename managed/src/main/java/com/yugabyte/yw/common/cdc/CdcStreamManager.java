/*
 * Copyright 2022 YugabyteDB, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 *     https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
package com.yugabyte.yw.common.cdc;

import com.cronutils.utils.VisibleForTesting;
import com.yugabyte.yw.common.NodeUniverseManager;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.forms.CDCReplicationSlotResponse;
import com.yugabyte.yw.forms.CDCReplicationSlotResponse.CDCReplicationSlotDetails;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CommonUtils;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Optional;
import javax.inject.Inject;
import javax.inject.Singleton;
import org.apache.commons.lang3.StringUtils;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.client.*;
import org.yb.master.MasterDdlOuterClass;
import org.yb.master.MasterReplicationOuterClass;
import org.yb.master.MasterTypes.NamespaceIdentifierPB;

@Singleton
public class CdcStreamManager {
  public static final Logger LOG = LoggerFactory.getLogger(CdcStreamManager.class);

  private final YBClientService ybClientService;
  private final NodeUniverseManager nodeUniverseManager;

  @Inject
  public CdcStreamManager(YBClientService clientService, NodeUniverseManager nodeUniverseManager) {
    this.ybClientService = clientService;
    this.nodeUniverseManager = nodeUniverseManager;
  }

  public List<CdcStream> getAllCdcStreams(Universe universe) throws Exception {
    try (YBClient client = ybClientService.getUniverseClient(universe)) {
      List<CdcStream> streams = new ArrayList<>();
      ListCDCStreamsResponse response =
          client.listCDCStreams(null, null, MasterReplicationOuterClass.IdTypePB.NAMESPACE_ID);

      LOG.info(
          "Got response for 'listCDCStreams' for universeId='{}': hasError='{}', size='{}'",
          universe.getUniverseUUID(),
          response.hasError(),
          response.getStreams() != null ? response.getStreams().size() : -1);

      if (response.hasError()) {
        throw new Exception(response.errorMessage());
      }

      for (CDCStreamInfo streamInfo : response.getStreams()) {
        HashMap<String, String> options = new HashMap<>(streamInfo.getOptions());
        CdcStream stream =
            new CdcStream(streamInfo.getStreamId(), options, streamInfo.getNamespaceId());
        streams.add(stream);
      }

      return streams;
    }
  }

  @VisibleForTesting
  protected YBTable getFirstTable(YBClient client, String databaseName) throws Exception {
    ListTablesResponse tablesResp = client.getTablesList();

    String tid = "";

    for (MasterDdlOuterClass.ListTablesResponsePB.TableInfo tableInfo :
        tablesResp.getTableInfoList()) {
      LOG.info("Found table: {} - {} ", tableInfo.getNamespace().getName(), tableInfo.getName());
      if (tableInfo.getNamespace().getName().equals(databaseName)) {
        tid = tableInfo.getId().toStringUtf8();
        // we found A table.
        break;
      }
    }

    if (tid.equals("")) {
      throw new Exception("Must have at least 1 table in database.");
    }

    return client.openTableByUUID(tid);
  }

  public CdcStreamCreateResponse createCdcStream(Universe universe, String databaseName)
      throws Exception {
    return createCdcStream(universe, databaseName, "PROTO", "IMPLICIT");
  }

  public CdcStreamCreateResponse createCdcStream(
      Universe universe, String databaseName, String format, String checkpointType)
      throws Exception {
    try (YBClient client = ybClientService.getUniverseClient(universe)) {

      LOG.info(
          "Creating CDC stream for universeId='{}' dbName='{}' format='{}', checkpointType='{}'",
          universe.getUniverseUUID(),
          databaseName,
          format,
          checkpointType);

      YBTable table = getFirstTable(client, databaseName);

      CreateCDCStreamResponse response =
          client.createCDCStream(table, databaseName, format, checkpointType);

      CdcStreamCreateResponse result = new CdcStreamCreateResponse(response.getStreamId());
      LOG.info(
          "Created CDC stream id='{}' for universeId='{}' dbName='{}' format='{}',"
              + " checkpointType='{}'",
          result.getStreamId(),
          universe.getUniverseUUID(),
          databaseName,
          format,
          checkpointType);
      return result;
    }
  }

  public CdcStreamDeleteResponse deleteCdcStream(Universe universe, String streamId)
      throws Exception {
    try (YBClient client = ybClientService.getUniverseClient(universe)) {
      HashSet<String> streamsToDelete = new HashSet<>();
      streamsToDelete.add(streamId);

      DeleteCDCStreamResponse response =
          client.deleteCDCStream(streamsToDelete, true /* ignoreErrors */, true /*
                                                                                 * forceDelete
                                                                                 */);

      if (response.hasError()) {
        throw new Exception(response.errorMessage());
      }

      return new CdcStreamDeleteResponse(new ArrayList<>(response.getNotFoundStreamIds()));
    }
  }

  public CDCReplicationSlotResponse listReplicationSlot(Universe universe) throws Exception {
    try (YBClient client = ybClientService.getUniverseClient(universe)) {

      ListCDCStreamsResponse response =
          client.listCDCStreams(null, null, MasterReplicationOuterClass.IdTypePB.NAMESPACE_ID);
      LOG.info(
          "Got response for 'listCDCStreams' for universeId='{}': hasError='{}', size='{}'",
          universe.getUniverseUUID(),
          response.hasError(),
          response.getStreams() != null ? response.getStreams().size() : -1);
      if (response.hasError()) {
        throw new Exception(response.errorMessage());
      }

      CDCReplicationSlotResponse result = new CDCReplicationSlotResponse();
      List<NamespaceIdentifierPB> namespaceList = null;
      for (CDCStreamInfo streamInfo : response.getStreams()) {
        HashMap<String, String> options = new HashMap<>(streamInfo.getOptions());
        CDCReplicationSlotDetails details = new CDCReplicationSlotDetails();
        details.streamID = streamInfo.getStreamId();
        details.slotName = streamInfo.getCdcsdkYsqlReplicationSlotName();
        // Skip cdc streams which does not have name as an replication slot is supposed
        // to have
        // name and yb-admin streams does not have name.
        if (StringUtils.isEmpty(details.slotName)) {
          LOG.info("Skipping slot {} in replication slot response.", details.slotName);
          continue;
        }

        String walStatus = null;
        NodeDetails randomTServer = null;
        try {
          randomTServer = CommonUtils.getARandomLiveTServer(universe);
        } catch (IllegalStateException ise) {
          LOG.error("Error while querying replication slot: status: {}", details.streamID, ise);
          continue;
        }
        String query =
            "SELECT wal_status "
                + "FROM pg_replication_slots "
                + "WHERE yb_stream_id='"
                + details.streamID
                + "';";
        ShellResponse shellResponse =
            nodeUniverseManager.runYsqlCommand(randomTServer, universe, "yugabyte", query);

        try {
          String commandOutput = shellResponse.extractRunCommandOutput();
          // commandOutput would be like
          //  wal_status
          //  ----------------
          //  lost
          //  (1 row)
          String[] lines = commandOutput.split("\n");
          if (lines.length >= 3) {
            walStatus = lines[2].trim();
            // walStatus will be "lost" or "reserved"

          }
        } catch (RuntimeException e) {
          LOG.error("Failed to extract wal_status from shell response:", e);
        }
        if ("reserved".equals(walStatus)) {
          details.state = "ACTIVE";
        } else if ("lost".equals(walStatus)) {
          details.state = "EXPIRED";
        } else {
          details.state = "UNKNOWN";
        }

        if (namespaceList == null) {
          namespaceList = client.getNamespacesList().getNamespacesList();
        }
        Optional<NamespaceIdentifierPB> namespaceIdentifierOptional =
            namespaceList.stream()
                .filter(
                    namespace ->
                        namespace.getId().toStringUtf8().equals(streamInfo.getNamespaceId()))
                .findFirst();
        if (!namespaceIdentifierOptional.isEmpty()) {
          details.databaseName = namespaceIdentifierOptional.get().getName();
        } else {
          LOG.warn("Namespace not found for namespaceId='{}'", streamInfo.getNamespaceId());
        }
        result.replicationSlots.add(details);
      }
      return result;
    }
  }
}
