package com.yugabyte.troubleshoot.ts.service;

import static org.assertj.core.api.Assertions.assertThat;

import com.yugabyte.troubleshoot.ts.models.UniverseDetails;
import java.util.List;
import java.util.UUID;
import org.junit.jupiter.api.Test;
import org.springframework.beans.factory.annotation.Autowired;
import org.testcontainers.shaded.com.google.common.collect.ImmutableList;
import org.testcontainers.shaded.com.google.common.collect.ImmutableSet;

@ServiceTest
public class UniverseDetailsServiceTest {

  @Autowired private UniverseDetailsService universeDetailsService;

  @Test
  public void testCRUD() {
    UniverseDetails details = testData();
    UniverseDetails saved = universeDetailsService.save(details);
    assertThat(saved).isEqualTo(details);

    UniverseDetails queried = universeDetailsService.get(details.getId());
    assertThat(queried).isEqualTo(details);
    queried.getUniverseDetails().setUniversePaused(true);

    UniverseDetails updated = universeDetailsService.save(queried);
    assertThat(updated).isEqualTo(queried);

    List<UniverseDetails> queriedById =
        universeDetailsService.listByIds(ImmutableList.of(details.getId()));
    assertThat(queriedById).containsExactly(updated);

    universeDetailsService.delete(details.getId());

    List<UniverseDetails> remaining = universeDetailsService.listAll();
    assertThat(remaining).isEmpty();
  }

  public static UniverseDetails testData() {
    return testData(UUID.randomUUID());
  }

  public static UniverseDetails testData(UUID universeUuid) {
    UUID clusterUuid = UUID.randomUUID();
    return new UniverseDetails()
        .setUniverseUUID(universeUuid)
        .setName("Universe 1")
        .setUniverseDetails(
            new UniverseDetails.UniverseDefinition()
                .setClusters(
                    ImmutableList.of(
                        new UniverseDetails.UniverseDefinition.Cluster()
                            .setUuid(clusterUuid)
                            .setClusterType("PRIMARY")
                            .setUserIntent(
                                new UniverseDetails.UniverseDefinition.UserIntent()
                                    .setYbSoftwareVersion("2.21.0.1_b12"))))
                .setNodePrefix("universe_1_node")
                .setNodeDetailsSet(
                    ImmutableSet.of(
                        new UniverseDetails.UniverseDefinition.NodeDetails()
                            .setNodeName("node1")
                            .setMaster(true)
                            .setTserver(false)
                            .setNodeUuid(UUID.randomUUID())
                            .setYsqlServer(false)
                            .setCloudInfo(
                                new UniverseDetails.UniverseDefinition.CloudSpecificInfo()
                                    .setCloud("aws")
                                    .setRegion("us-west-1")
                                    .setAz("us-west-1a")
                                    .setKubernetesPodName("n1_pod")
                                    .setKubernetesNamespace("yb_us_west_1_ns")),
                        new UniverseDetails.UniverseDefinition.NodeDetails()
                            .setNodeName("node2")
                            .setMaster(false)
                            .setTserver(true)
                            .setNodeUuid(UUID.randomUUID())
                            .setYsqlServer(true)
                            .setCloudInfo(
                                new UniverseDetails.UniverseDefinition.CloudSpecificInfo()
                                    .setCloud("k8s")
                                    .setRegion("us-west-1")
                                    .setAz("us-west-1a")
                                    .setKubernetesPodName("n2_pod")
                                    .setKubernetesNamespace("yb_us_west_1_ns")),
                        new UniverseDetails.UniverseDefinition.NodeDetails()
                            .setNodeName("node3")
                            .setMaster(false)
                            .setTserver(true)
                            .setNodeUuid(UUID.randomUUID())
                            .setYsqlServer(true)
                            .setCloudInfo(
                                new UniverseDetails.UniverseDefinition.CloudSpecificInfo()
                                    .setCloud("k8s")
                                    .setRegion("us-west-1")
                                    .setAz("us-west-1b")
                                    .setKubernetesPodName("n3_pod")
                                    .setKubernetesNamespace("yb_us_west_1_ns")),
                        new UniverseDetails.UniverseDefinition.NodeDetails()
                            .setNodeName("node4")
                            .setMaster(false)
                            .setTserver(true)
                            .setNodeUuid(UUID.randomUUID())
                            .setYsqlServer(true)
                            .setCloudInfo(
                                new UniverseDetails.UniverseDefinition.CloudSpecificInfo()
                                    .setCloud("k8s")
                                    .setRegion("us_west-2")
                                    .setAz("us-west-2a")
                                    .setKubernetesPodName("n4_pod")
                                    .setKubernetesNamespace("yb_us_west_2_ns")))));
  }
}
