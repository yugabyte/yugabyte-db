package com.yugabyte.yw.common.cdc;

import com.azure.core.annotation.Get;
import com.cronutils.utils.VisibleForTesting;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.models.Universe;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.client.*;
import org.yb.master.MasterDdlOuterClass;
import org.yb.master.MasterReplicationOuterClass;

import javax.inject.Inject;
import javax.inject.Singleton;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;

@Singleton
public class CdcStreamManager {
  public static final Logger LOG = LoggerFactory.getLogger(CdcStreamManager.class);

  private final YBClientService ybClientService;

  @Inject
  public CdcStreamManager(YBClientService clientService) {
    this.ybClientService = clientService;
  }

  private YBClient getYBClientForUniverse(Universe universe) {
    LOG.info("Getting YBClient for universeId='{}'", universe.universeUUID);

    String masterAddresses = universe.getMasterAddresses();
    String certificate = universe.getCertificateNodetoNode();

    LOG.info("Masters for universeId='{}' are: {}", universe.universeUUID, masterAddresses);

    try {
      YBClient client = ybClientService.getClient(masterAddresses, certificate);
      LOG.info("Got client for universeId='{}'", universe.universeUUID);
      return client;
    } catch (Exception ex) {
      LOG.error("Exception while trying to getYBClientForUniverse.", ex);
      throw new RuntimeException(ex);
    }
  }

  public List<CdcStream> getAllCdcStreams(Universe universe) throws Exception {
    try (YBClient client = getYBClientForUniverse(universe)) {
      List<CdcStream> streams = new ArrayList<>();
      ListCDCStreamsResponse response =
          client.listCDCStreams(null, null, MasterReplicationOuterClass.IdTypePB.NAMESPACE_ID);

      LOG.info(
          "Got response for 'listCDCStreams' for universeId='{}': hasError='{}', size='{}'",
          universe.universeUUID,
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
    try (YBClient client = getYBClientForUniverse(universe)) {

      LOG.info(
          "Creating CDC stream for universeId='{}' dbName='{}' format='{}', checkpointType='{}'",
          universe.universeUUID,
          databaseName,
          format,
          checkpointType);

      YBTable table = getFirstTable(client, databaseName);

      CreateCDCStreamResponse response =
          client.createCDCStream(table, databaseName, format, checkpointType);

      CdcStreamCreateResponse result = new CdcStreamCreateResponse(response.getStreamId());
      LOG.info(
          "Created CDC stream id='{}' for universeId='{}' dbName='{}' format='{}', checkpointType='{}'",
          result.getStreamId(),
          universe.universeUUID,
          databaseName,
          format,
          checkpointType);
      return result;
    }
  }

  public CdcStreamDeleteResponse deleteCdcStream(Universe universe, String streamId)
      throws Exception {
    try (YBClient client = getYBClientForUniverse(universe)) {
      HashSet<String> streamsToDelete = new HashSet<>();
      streamsToDelete.add(streamId);

      DeleteCDCStreamResponse response =
          client.deleteCDCStream(streamsToDelete, true /*ignoreErrors*/, true /*forceDelete*/);

      if (response.hasError()) {
        throw new Exception(response.errorMessage());
      }

      return new CdcStreamDeleteResponse(new ArrayList<>(response.getNotFoundStreamIds()));
    }
  }
}
