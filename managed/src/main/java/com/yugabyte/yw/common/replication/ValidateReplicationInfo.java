package com.yugabyte.yw.common.replication;

import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.backuprestore.Tablespace;
import java.util.ArrayList;
import java.util.Collection;
import java.util.List;
import java.util.UUID;
import java.util.stream.Collectors;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections.CollectionUtils;
import org.yb.client.YBClient;

@Slf4j
@Singleton
public class ValidateReplicationInfo {

  private final YBClientService ybClientService;

  @Inject
  public ValidateReplicationInfo(YBClientService ybClientService) {
    this.ybClientService = ybClientService;
  }

  public List<Tablespace> getUnsupportedTablespacesOnUniverse(
      Universe universe, Collection<Tablespace> tablespaces) {
    String universeMasterAddresses = universe.getMasterAddresses(true /* mastersQueryable */);
    String universeCertificate = universe.getCertificateNodetoNode();
    UUID primaryClusterPlacementUUID = universe.getUniverseDetails().getPrimaryCluster().uuid;
    if (CollectionUtils.isNotEmpty(tablespaces)) {
      try (YBClient client =
          ybClientService.getClient(universeMasterAddresses, universeCertificate)) {
        return tablespaces.stream()
            .filter(
                t -> {
                  try {
                    client.validateReplicationInfo(
                        t.replicaPlacement.getReplicationInfoPB(primaryClusterPlacementUUID));
                    return false;
                  } catch (Exception e) {
                    if (e.getMessage().contains("INVALID_ARGUMENT")) {
                      log.error(
                          "Tablespace {} with replica placement: {}"
                              + " validation failed on Universe {},"
                              + " error: {}",
                          t.tablespaceName,
                          t.getJsonString(),
                          universe.getName(),
                          e.getMessage());
                      return true;
                    }
                    throw new RuntimeException(e);
                  }
                })
            .collect(Collectors.toList());
      } catch (Exception e) {
        log.error(
            "Unable to validate tablespaces on universe {}, error: {}",
            universe.getName(),
            e.getMessage());
        throw new RuntimeException(e);
      }
    }
    return new ArrayList<>();
  }
}
