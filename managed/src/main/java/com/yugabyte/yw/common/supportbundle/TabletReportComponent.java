package com.yugabyte.yw.common.supportbundle;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.net.HostAndPort;
import com.google.inject.Inject;
import com.google.protobuf.util.JsonFormat;
import com.yugabyte.yw.commissioner.tasks.params.SupportBundleTaskParams;
import com.yugabyte.yw.common.NodeUIApiHelper;
import com.yugabyte.yw.common.SupportBundleUtil;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.forms.SupportBundleFormData;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.Date;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import lombok.extern.slf4j.Slf4j;
import org.yb.Common.PartitionSchemaPB;
import org.yb.Common.SchemaPB;
import org.yb.CommonTypes.TableType;
import org.yb.Opid.OpIdPB;
import org.yb.client.GetConsensusStateResponse;
import org.yb.client.GetLatestEntryOpIdResponse;
import org.yb.client.ListTabletsResponse;
import org.yb.client.YBClient;
import org.yb.tablet.Tablet.TabletStatusPB;
import org.yb.tserver.Tserver.ListTabletsResponsePB.StatusAndSchemaPB;
import play.libs.Json;

@Slf4j
public class TabletReportComponent implements SupportBundleComponent {

  private final NodeUIApiHelper apiHelper;
  private final SupportBundleUtil supportBundleUtil;
  private final YBClientService ybClientService;

  @Inject
  TabletReportComponent(
      NodeUIApiHelper apiHelper,
      SupportBundleUtil supportBundleUtil,
      YBClientService ybClientService) {
    this.apiHelper = apiHelper;
    this.supportBundleUtil = supportBundleUtil;
    this.ybClientService = ybClientService;
  }

  public void downloadComponent(
      SupportBundleTaskParams supportBundleTaskParams,
      Customer customer,
      Universe universe,
      Path bundlePath,
      NodeDetails node)
      throws Exception {
    Path path = Files.createDirectories(Paths.get(bundlePath.toString(), "TabletReport"));
    supportBundleUtil.saveMetadata(
        customer,
        path.toString(),
        Json.parse(universe.getUniverseDetailsJson()),
        "universe-details.json");

    // Save master leader's view of all tablets in the report.
    try {
      String masterLeaderHost = universe.getMasterLeaderHostText();
      int masterHttpPort = universe.getMasterLeaderNode().masterHttpPort;
      String protocol =
          universe.getUniverseDetails().getPrimaryCluster().userIntent.enableClientToNodeEncrypt
              ? "https"
              : "http";
      String url =
          String.format("%s://%s:%d/dump-entities", protocol, masterLeaderHost, masterHttpPort);
      log.info("Querying url {} for dump entities.", url);
      JsonNode response = apiHelper.getRequest(url);
      if (response.has("error")) {
        throw new RuntimeException(response.get("error").asText());
      }
      supportBundleUtil.saveMetadata(customer, path.toString(), response, "dump-entities.json");
    } catch (Exception e) {
      log.error("Error while collecting dump entities for universe: {} ", universe.getName(), e);
    }

    // Create tablet report from each tserver.
    String masterAddresses = universe.getMasterAddresses();
    String certificate = universe.getCertificateNodetoNode();
    try (YBClient client = ybClientService.getClient(masterAddresses, certificate)) {
      universe.getTServersInPrimaryCluster().parallelStream()
          .forEach(
              tserver -> {
                try {
                  String host =
                      (tserver.cloudInfo.private_ip == null
                          ? tserver.cloudInfo.private_dns
                          : tserver.cloudInfo.private_ip);
                  if (host == null) {
                    throw new RuntimeException("Host not found for tserver: " + tserver.nodeName);
                  }
                  HostAndPort tserverHP = HostAndPort.fromParts(host, tserver.tserverRpcPort);

                  log.info(
                      "Start of tablet report collection from node: {}, {}:{}.",
                      tserver.nodeName,
                      host,
                      tserver.tserverRpcPort);

                  // Get all tablets for the current Tserver.
                  ListTabletsResponse tablets =
                      client.listStatusAndSchemaOfTabletsForTServer(tserverHP);
                  List<String> tabletIds =
                      tablets.getStatusAndSchemaPBs().stream()
                          .filter(
                              pb ->
                                  !pb.getTabletStatus()
                                      .getTableType()
                                      .equals(TableType.TRANSACTION_STATUS_TABLE_TYPE))
                          .map(pb -> pb.getTabletStatus().getTabletId())
                          .toList();

                  // Map each tablet to its latest entry's OpId.
                  Map<String, OpIdPB> tabletToOpId = new HashMap<>();
                  GetLatestEntryOpIdResponse response =
                      client.getLatestEntryOpIds(tserverHP, tabletIds);
                  if (response.hasError()) {
                    log.warn("Error when getting latest op ids = {}", response.getError());
                  }
                  if (tabletIds.size() == response.getOpIds().size()) {
                    List<OpIdPB> opIds = response.getOpIds();
                    for (int i = 0; i < tabletIds.size(); i++) {
                      tabletToOpId.put(tabletIds.get(i), opIds.get(i));
                    }
                  }

                  ObjectNode rootNode = Json.newObject();
                  ArrayNode content = Json.newArray();
                  rootNode.put("msg", "Tablet report.");

                  // Loop through individual tablets to orgranise info in ObjectNdoe.
                  for (StatusAndSchemaPB sasPB : tablets.getStatusAndSchemaPBs()) {
                    ObjectNode tabletNode = Json.newObject();
                    String tabletId = sasPB.getTabletStatus().getTabletId();
                    GetConsensusStateResponse cstate =
                        client.getTabletConsensusStateFromTS(tabletId, tserverHP);

                    ObjectNode consensusStateNode = Json.newObject();
                    consensusStateNode.set(
                        "cstate",
                        Json.parse(JsonFormat.printer().print(cstate.getConsensusState())));
                    consensusStateNode.put(
                        "leaderLeaseStatus", cstate.getLeaderLeaseStatus().toString());

                    // Set latest entry's OpId index and term.
                    if (tabletToOpId.containsKey(tabletId)) {
                      OpIdPB opId = tabletToOpId.get(tabletId);
                      ((ObjectNode) consensusStateNode.get("cstate").get("config"))
                          .put("opidIndex", opId.getIndex());
                      ((ObjectNode) consensusStateNode.get("cstate").get("config"))
                          .put("opidTerm", opId.getTerm());
                    }
                    tabletNode.set("consensus_state", consensusStateNode);

                    TabletStatusPB tabletStatus = sasPB.getTabletStatus();
                    ObjectNode tablet = Json.newObject();
                    SchemaPB schema = sasPB.getSchema();
                    PartitionSchemaPB partitionSchema = sasPB.getPartitionSchema();
                    tablet.set(
                        "tablet_status", Json.parse(JsonFormat.printer().print(tabletStatus)));
                    tablet.set("schema", Json.parse(JsonFormat.printer().print(schema)));
                    tablet.set(
                        "partition_schema",
                        Json.parse(JsonFormat.printer().print(partitionSchema)));
                    tabletNode.set("tablet", tablet);

                    content.add(tabletNode);
                  }
                  rootNode.set("content", content);
                  supportBundleUtil.saveMetadata(
                      customer,
                      path.toString(),
                      rootNode,
                      tserver.nodeName + "_tablet_report.json");
                } catch (Exception e) {
                  log.error(
                      "Error while collecting tablet report from tserver {}: ",
                      tserver.nodeName,
                      e);
                }
              });
    } catch (Exception e) {
      log.error(
          "Error occurred while collecting tablet report for universe {}:", universe.getName(), e);
    }
  }

  public void downloadComponentBetweenDates(
      SupportBundleTaskParams supportBundleTaskParams,
      Customer customer,
      Universe universe,
      Path bundlePath,
      Date startDate,
      Date endDate,
      NodeDetails node)
      throws Exception {
    downloadComponent(supportBundleTaskParams, customer, universe, bundlePath, node);
  }

  // Returns estimated size based on the number of tablets present on a tserver.
  public Map<String, Long> getFilesListWithSizes(
      Customer customer,
      SupportBundleFormData bundleData,
      Universe universe,
      Date startDate,
      Date endDate,
      NodeDetails node)
      throws Exception {
    Map<String, Long> res = new HashMap<>();
    // ~12KB for universe details.
    long size = 12000;
    String masterAddresses = universe.getMasterAddresses();
    String certificate = universe.getCertificateNodetoNode();
    try (YBClient client = ybClientService.getClient(masterAddresses, certificate)) {
      List<NodeDetails> tservers = universe.getTServersInPrimaryCluster();
      int numTservers = tservers.size();
      NodeDetails tserver = tservers.get(0);
      String host =
          (tserver.cloudInfo.private_ip == null
              ? tserver.cloudInfo.private_dns
              : tserver.cloudInfo.private_ip);
      if (host == null) {
        throw new RuntimeException("Host not found for tserver: " + tserver.nodeName);
      }
      HostAndPort tserverHP = HostAndPort.fromParts(host, tserver.tserverRpcPort);
      int numTablets =
          client.listStatusAndSchemaOfTabletsForTServer(tserverHP).getStatusAndSchemaPBs().size();
      // Add 4KB per tablet per Tserver.
      size += 1L * 4000 * numTablets * numTservers;
      // Add 400B per tablet for dump-entities.
      size += 1L * 400 * numTablets;
    }
    res.put("TabletReportSize", size);
    return res;
  }
}
