// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks.xcluster;

import com.google.api.client.util.Throwables;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.XClusterConfigTaskBase;
import com.yugabyte.yw.common.XClusterUniverseService;
import com.yugabyte.yw.forms.XClusterConfigTaskParams;
import com.yugabyte.yw.models.Universe;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.stream.Collectors;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.yb.CommonTypes.YQLDatabase;
import org.yb.client.CDCStreamInfo;
import org.yb.client.DeleteCDCStreamResponse;
import org.yb.client.GetNamespaceInfoResponse;
import org.yb.client.ListCDCStreamsResponse;
import org.yb.client.YBClient;
import org.yb.master.MasterDdlOuterClass;

@Slf4j
public class DeleteRemnantStreams extends XClusterConfigTaskBase {

  @Inject
  protected DeleteRemnantStreams(
      BaseTaskDependencies baseTaskDependencies, XClusterUniverseService xClusterUniverseService) {
    super(baseTaskDependencies, xClusterUniverseService);
  }

  public static class Params extends XClusterConfigTaskParams {
    public String namespaceName;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public void run() {
    Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
    String universeMasterAddresses = universe.getMasterAddresses();
    String universeCertificate = universe.getCertificateNodetoNode();

    try (YBClient client = ybService.getClient(universeMasterAddresses, universeCertificate)) {

      GetNamespaceInfoResponse getNamespaceInfoResponse =
          client.getNamespaceInfo(taskParams().namespaceName, YQLDatabase.YQL_DATABASE_PGSQL);
      if (getNamespaceInfoResponse.hasError()) {
        throw new RuntimeException(
            String.format(
                "Error getting namespace details for namespace: %s from universe %s. Error: %s",
                taskParams().namespaceName,
                universe.getName(),
                getNamespaceInfoResponse.errorMessage()));
      }

      String namespaceId = getNamespaceInfoResponse.getNamespaceId();
      log.debug(
          "Namespace id: {} found for with namespace name: {} on universe: {}",
          namespaceId,
          taskParams().namespaceName,
          universe.getName());

      List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> tableInfoList =
          XClusterConfigTaskBase.getTableInfoList(ybService, universe).stream()
              .filter(
                  tableInfo -> tableInfo.getNamespace().getId().toStringUtf8().equals(namespaceId))
              .collect(Collectors.toList());
      Set<String> tableIds = XClusterConfigTaskBase.getTableIds(tableInfoList);

      // Find all streams for universe.
      ListCDCStreamsResponse cdcStreamsResponse = client.listCDCStreams(null, null, null);

      if (cdcStreamsResponse.hasError()) {
        throw new RuntimeException(
            String.format(
                "Error listing cdc streams for universe %s with namespace id: %s. Error: %s",
                universe.getName(), namespaceId, cdcStreamsResponse.errorMessage()));
      }

      Set<String> streamIdsToDelete = new HashSet<>();
      for (CDCStreamInfo cdcStream : cdcStreamsResponse.getStreams()) {
        for (String tableId : cdcStream.getTableIds()) {
          if (tableIds.contains(tableId)) {
            streamIdsToDelete.add(cdcStream.getStreamId());
            break;
          }
        }
      }

      log.debug(
          "Remnant stream ids remaining(truncated): {}",
          streamIdsToDelete.stream().limit(20).collect(Collectors.toSet()));
      if (streamIdsToDelete.isEmpty()) {
        log.debug("There are no dangling streams, skipping clean up ...");
        return;
      }

      // Need force delete = true for dangling streams.
      DeleteCDCStreamResponse deleteCDCStreamResponse =
          client.deleteCDCStream(
              streamIdsToDelete, false /* ignoreErrors */, true /* forceDelete */);

      if (deleteCDCStreamResponse.hasError()) {
        throw new RuntimeException(
            String.format(
                "Error deleting cdc streams for namespace: %s for universe: %s. Error: %s",
                taskParams().namespaceName,
                universe.getName(),
                deleteCDCStreamResponse.errorMessage()));
      }

      log.debug("Successfully deleted all remnant streams.");
    } catch (Exception e) {
      if (e.getMessage().toLowerCase().contains("keyspace name not found")) {
        log.debug("Namespace {} does not exist. Skipping clean up ...", taskParams().namespaceName);
        return;
      }
      log.error("{} hit error : {}", getName(), e.getMessage());
      Throwables.propagate(e);
    }
  }
}
