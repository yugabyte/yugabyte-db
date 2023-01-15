// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import static org.hamcrest.MatcherAssert.assertThat;
import static org.hamcrest.Matchers.hasItem;
import static org.hamcrest.Matchers.lessThanOrEqualTo;
import static org.hamcrest.Matchers.not;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;
import static org.mockito.Mockito.doReturn;
import static org.mockito.Mockito.mock;

import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.common.utils.FileUtils;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.ClusterType;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import java.io.File;
import java.text.SimpleDateFormat;
import java.time.Duration;
import java.util.Date;
import java.util.HashSet;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.TimeUnit;
import junitparams.JUnitParamsRunner;
import junitparams.Parameters;
import junitparams.converters.Nullable;
import junitparams.naming.TestCaseName;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnit;
import org.mockito.junit.MockitoRule;

@RunWith(JUnitParamsRunner.class)
public class UtilTest extends FakeDBApplication {

  @Rule public MockitoRule mockitoRule = MockitoJUnit.rule();

  @Test
  public void testGetNodePrefixWithInvalidCustomerId() {
    try {
      Util.getNodePrefix(1L, "demo");
    } catch (Exception e) {
      assertEquals("Invalid Customer Id: 1", e.getMessage());
    }
  }

  @Test
  public void testGetNodePrefix() {
    Customer c = ModelFactory.testCustomer();
    String nodePrefix = Util.getNodePrefix(c.getCustomerId(), "demo");
    assertEquals("yb-tc-demo", nodePrefix);
  }

  @Test
  public void testGetKeysNotPresent() {
    Map<String, String> existing =
        ImmutableMap.of(
            "Cust", "Test",
            "Dept", "HR",
            "Remove", "This",
            "This", "Also");
    Map<String, String> newMap =
        ImmutableMap.of(
            "Cust", "Test",
            "Dept", "NewHR");
    assertEquals(Util.getKeysNotPresent(existing, newMap), "This,Remove");

    newMap =
        ImmutableMap.of(
            "Cust", "Test",
            "Dept", "NewHR",
            "Remove", "ABCD",
            "This", "BCDF",
            "Just", "Coz");
    assertEquals(Util.getKeysNotPresent(existing, newMap), "");

    assertEquals(existing.size(), 4);
    assertEquals(newMap.size(), 5);
  }

  private static NodeDetails createNode(UUID uuid, boolean isMaster, NodeState state) {
    NodeDetails node = new NodeDetails();
    node.azUuid = uuid;
    node.isMaster = isMaster;
    node.state = state;
    return node;
  }

  @Test
  @Parameters(method = "parametersToTestNeedMasterQuorumRestore")
  @TestCaseName("{method} TC({0}) Zones count {0} nodes per zones {1}")
  public void testNeedMasterQuorumRestore(
      String tcName,
      int azCount,
      int[] countsInAZ,
      int[] mastersInAZ,
      int[] stoppedInAZ,
      int mastersToBeAdded,
      boolean[] expectedResults) {

    UUID[] azUUIDs = new UUID[azCount];
    for (int i = 0; i < azCount; i++) {
      azUUIDs[i] = UUID.randomUUID();
    }
    Cluster cluster = new Cluster(ClusterType.PRIMARY, null);
    Set<NodeDetails> nodeDetailsSet =
        prepareNodes(cluster, azUUIDs, azCount, countsInAZ, mastersInAZ, stoppedInAZ);

    // Going through all the zones trying to add a node
    NodeDetails currentNode = createNode(null, false, NodeState.Stopped);
    for (int i = 0; i < azCount; i++) {
      currentNode.azUuid = azUUIDs[i];
      assertEquals(
          "Zone to probe " + i,
          expectedResults[i],
          Util.needMasterQuorumRestore(currentNode, nodeDetailsSet, mastersToBeAdded));
    }
  }

  private static Set<NodeDetails> prepareNodes(
      Cluster cluster,
      UUID[] azUUIDs,
      int azCount,
      int[] countsInAZ,
      int[] mastersInAZ,
      int[] stoppedInAZ) {
    Set<NodeDetails> nodeDetailsSet = new HashSet<>();
    for (int i = 0; i < azCount; i++) {
      int mastersToPlace = mastersInAZ[i];
      int stoppedCount = stoppedInAZ[i];
      for (int j = 0; j < countsInAZ[i]; j++) {
        boolean isStopped = (mastersToPlace <= 0) && (stoppedCount > 0);
        if (isStopped) {
          stoppedCount--;
        }
        NodeDetails node =
            createNode(
                azUUIDs[i], mastersToPlace > 0, isStopped ? NodeState.Stopped : NodeState.Live);
        node.placementUuid = cluster.uuid;
        nodeDetailsSet.add(node);
        mastersToPlace--;
      }
    }
    return nodeDetailsSet;
  }

  @SuppressWarnings("unused")
  private Object[] parametersToTestNeedMasterQuorumRestore() {
    return new Object[][] {
      // One zone without masters, one master to place
      {"A", 1, $(1), $(0), $(1), 1, $(true)},
      // Three zones, one stopped node in each zone
      // TODO: placement into z2 should not be allowed because masters should
      // be placed evenly; this will be fixed later
      {"B", 3, $(2, 3, 2), $(1, 2, 1), $(1, 1, 1), 1, $(true, true, true)},
      // Three zones, no masters in z1; should not allow to place master in z2 and z3
      {"C", 3, $(1, 1, 1), $(0, 1, 1), $(1, 0, 0), 1, $(true, false, false)},
    };
  }

  @Test
  @Parameters(method = "parametersToTestAreMastersUnderReplicated")
  @TestCaseName("{method} TC({0}) Zones count {1} RF {2} nodes per zones {3}")
  public void testAreMastersUnderReplicated(
      String tcName,
      int azCount,
      int replicationFactor,
      int[] countsInAZ,
      int[] mastersInAZ,
      int[] stoppedInAZ,
      boolean[] expectedResults) {

    UUID[] azUUIDs = new UUID[azCount];
    for (int i = 0; i < azCount; i++) {
      azUUIDs[i] = UUID.randomUUID();
    }

    UniverseDefinitionTaskParams taskParams = new UniverseDefinitionTaskParams();
    Cluster cluster = new Cluster(ClusterType.PRIMARY, new UserIntent());
    cluster.uuid = UUID.randomUUID();
    cluster.userIntent.replicationFactor = replicationFactor;
    taskParams.clusters.add(cluster);
    taskParams.nodeDetailsSet =
        prepareNodes(cluster, azUUIDs, azCount, countsInAZ, mastersInAZ, stoppedInAZ);

    Universe universe = mock(Universe.class);
    doReturn(taskParams.nodeDetailsSet).when(universe).getNodes();
    doReturn(taskParams).when(universe).getUniverseDetails();
    doReturn(cluster).when(universe).getCluster(cluster.uuid);

    // Going through all the zones trying to add a node
    NodeDetails currentNode = createNode(null, false, NodeState.Stopped);
    currentNode.placementUuid = cluster.uuid;
    for (int i = 0; i < azCount; i++) {
      currentNode.azUuid = azUUIDs[i];
      assertEquals(
          "Zone to probe " + i,
          expectedResults[i],
          Util.areMastersUnderReplicated(currentNode, universe));
    }
  }

  @SuppressWarnings("unused")
  private Object[] parametersToTestAreMastersUnderReplicated() {
    return new Object[][] {
      // AZ=1, RF=1, no running masters, test node in zone 0 => true
      {"A", 1, 1, $(1), $(0), $(1), $(true)},
      // AZ=1, RF=1, one master is running, test node in zone 0 => false
      {"B", 1, 1, $(1), $(1), $(1), $(false)},

      // AZ=3, RF=3, one stopped node in each zone, one master in each zone
      {"C", 3, 3, $(2, 2, 2), $(1, 1, 1), $(1, 1, 1), $(false, false, false)},
      // AZ=3, RF=3, one stopped node in each zone, no masters in z2
      {"D", 3, 3, $(2, 1, 2), $(1, 0, 1), $(1, 1, 1), $(false, true, false)},
      // AZ=3, RF=3, no masters in z1; should not allow to place master in z2 and z3
      {"E", 3, 3, $(1, 1, 1), $(0, 1, 1), $(1, 0, 0), $(true, false, false)},
      // AZ=3, RF=3, one stopped node in each zone, no masters at all
      {"F", 3, 3, $(1, 1, 1), $(0, 0, 0), $(1, 1, 1), $(true, true, true)},

      // AZ=3, RF=5, one stopped node in each zone
      // TODO: placement into second zone should not be allowed because masters should
      // be placed evenly; this will be fixed later
      {"G", 3, 5, $(2, 3, 2), $(1, 2, 1), $(1, 1, 1), $(true, true, true)},
      // AZ=3, RF=5, one stopped node in each zone, all masters are already placed
      {"H", 3, 5, $(2, 2, 2), $(2, 2, 1), $(1, 1, 1), $(false, false, false)},
      // AZ=3, RF=5, the same scenario, no stopped nodes
      {"I", 3, 5, $(2, 2, 2), $(2, 2, 1), $(0, 0, 0), $(false, false, false)},
    };
  }

  @SafeVarargs
  public static int[] $(int... values) {
    return values;
  }

  @SafeVarargs
  public static boolean[] $(boolean... values) {
    return values;
  }

  @Test
  public void testAreMastersUnderReplicatedForReadReplicaNode() {
    int azCount = 3;
    UUID[] azUUIDs = new UUID[azCount];
    for (int i = 0; i < azCount; i++) {
      azUUIDs[i] = UUID.randomUUID();
    }

    UniverseDefinitionTaskParams taskParams = new UniverseDefinitionTaskParams();
    Cluster cluster = new Cluster(ClusterType.PRIMARY, new UserIntent());
    cluster.uuid = UUID.randomUUID();
    cluster.userIntent.replicationFactor = 3;
    taskParams.clusters.add(cluster);
    taskParams.nodeDetailsSet =
        prepareNodes(cluster, azUUIDs, azCount, $(3, 3, 3), $(0, 0, 0), $(1, 1, 1));

    Cluster replicaCluster = new Cluster(ClusterType.ASYNC, new UserIntent());
    replicaCluster.uuid = UUID.randomUUID();
    taskParams.clusters.add(replicaCluster);

    Universe universe = mock(Universe.class);
    doReturn(taskParams.nodeDetailsSet).when(universe).getNodes();
    doReturn(taskParams).when(universe).getUniverseDetails();
    doReturn(cluster).when(universe).getCluster(cluster.uuid);
    doReturn(replicaCluster).when(universe).getCluster(replicaCluster.uuid);

    NodeDetails currentNode = createNode(null, false, NodeState.Stopped);
    currentNode.placementUuid = replicaCluster.uuid;
    taskParams.nodeDetailsSet.add(currentNode);

    assertFalse(Util.areMastersUnderReplicated(currentNode, universe));
  }

  @Test
  // @formatter:off
  @Parameters({
    "filename, filename", // no path
    ",", // empty name
    "null, null", // null values
    "/test, test",
    "/part1/part2/filename, filename",
    "/part1/part2/,"
  })
  // @formatter:on
  public void testGetFileName(@Nullable String fullName, @Nullable String fileName) {
    assertEquals(fileName, FileUtils.getFileName(fullName));
  }

  // TODO: Add tests for other functions

  @Test
  public void testCompareYbVersions() {
    assertEquals(0, Util.compareYbVersions("2.5.2.0", "2.5.2.0"));
    assertEquals(0, Util.compareYbVersions("2.5.2.0-b5", "2.5.2.0-b5"));

    assertTrue(Util.compareYbVersions("2.5.2.1", "2.5.2.0") > 0);
    assertTrue(Util.compareYbVersions("3.5.2.0", "2.5.2.0") > 0);
    assertTrue(Util.compareYbVersions("2.5.2.1-b10", "2.5.2.0-b5") > 0);
    assertTrue(Util.compareYbVersions("2.5.2.0-b10", "2.5.2.0-b5") > 0);

    assertTrue(Util.compareYbVersions("2.5.2.0", "2.5.2.1") < 0);
    assertTrue(Util.compareYbVersions("2.5.2.0", "3.5.2.0") < 0);
    assertTrue(Util.compareYbVersions("2.5.2.0-b5", "2.5.2.1-b10") < 0);
    assertTrue(Util.compareYbVersions("2.5.2.0-b5", "2.5.2.0-b10") < 0);

    assertEquals(0, Util.compareYbVersions("2.5.2.0-b5", "2.5.2.0-custom"));
    assertEquals(0, Util.compareYbVersions("2.5.2.0-custom1", "2.5.2.0-custom2"));

    Exception exception =
        assertThrows(RuntimeException.class, () -> Util.compareYbVersions("2.2-b5", "2.6.50"));
    assertEquals("Unable to parse YB version strings", exception.getMessage());
  }

  @Test
  public void testRemoveEnclosingDoubleQuotes() {
    // Removes, happy path.
    assertEquals("baz", Util.removeEnclosingDoubleQuotes("\"baz\""));
    // Doesn't remove single internal quotes
    assertEquals("ba\"z", Util.removeEnclosingDoubleQuotes("\"ba\"z\""));
    // Doesn't remove pair of internal quotes.
    assertEquals("a\"ba\"z", Util.removeEnclosingDoubleQuotes("a\"ba\"z"));
    // Doesn't remove only starting quotes.
    assertEquals("\"baz", Util.removeEnclosingDoubleQuotes("\"baz"));
    // Doesn't remove only ending quotes.
    assertEquals("baz\"", Util.removeEnclosingDoubleQuotes("baz\""));
    // Empty string
    assertEquals("", Util.removeEnclosingDoubleQuotes(""));
    // Null string
    assertNull(Util.removeEnclosingDoubleQuotes(null));
  }

  @Test
  public void testDeleteDirectory() {
    File folder = new File("certificates");
    folder.mkdir();
    new File("certificates/ca.cert");
    new File("certificates/cb.cert");
    FileUtils.deleteDirectory(folder);
    assertFalse(folder.exists());
  }

  @Test
  @Parameters({
    "yyyy-MM-dd HH:mm:ss",
    "yyyy-MM-dd'T'HH:mm:ss",
    "EEE MMM dd HH:mm:ss zzz yyyy",
    "yyyyMMddhhmm"
  })
  public void testGetUnixTimeFromString(String timeStampFormat) {
    try {
      Date date = new Date();
      SimpleDateFormat dateFormat = new SimpleDateFormat(timeStampFormat);
      String currDate = dateFormat.format(date);
      long currentTimeUnix = Util.microUnixTimeFromDateString(currDate, timeStampFormat);
      String recievedDate = dateFormat.format(new Date(currentTimeUnix / 1000L));
      assertEquals(currDate, recievedDate);
    } catch (Exception e) {
      assertNull(e);
    }
  }

  @Test
  public void testHashStringNoConflict() {
    Set<String> hashes = new HashSet<>();
    for (int i = 0; i < 50; i++) {
      String name = "MyTestUniverse-" + i;
      String hash = Util.hashString(name);
      assertThat(hashes, not(hasItem(hash)));
      hashes.add(hash);
      hash = Util.hashString(name.toLowerCase());
      assertThat(hashes, not(hasItem(hash)));
      hashes.add(hash);
    }
  }

  @Test
  @Parameters({
    "MyTestUniverse1, mytestuniverse1-07f44fae",
    "mytestuniverse1, mytestuniverse1",
    "MyTestUniverseMyTestUniverseMyTestUniverseMyTestUniverseMyTestUniverse1,"
        + "mytestuniversemytestuniversemytestuniversemytestuniver-6d67fa22"
  })
  public void testSanitizeKubernetesNamespace(String univName, String expectedNamespace) {
    String namespace = Util.sanitizeKubernetesNamespace(univName, 0);
    assertEquals(expectedNamespace, namespace);
    assertThat("Max namespace length", namespace.length(), lessThanOrEqualTo(63));
  }

  @Parameters({
    "CREATE USER FOO PASSWORD REDACTED; CREATE USER BAR PASSWORD REDACTED;,"
        + "CREATE USER FOO PASSWORD 'fooBar'; CREATE USER BAR PASSWORD 'fooBar';",
    "\"ALTER USER \"yugabyte\" WITH PASSWORD REDACTED; "
        + "SELECT pg_stat_statements_reset();\"',"
        + "\"ALTER USER \"yugabyte\" WITH PASSWORD '\"'\"'FO@BAR'\"'\"'; "
        + "SELECT pg_stat_statements_reset();\"'"
  })
  @Test
  public void testYsqlRedaction(String output, String input) {
    assertEquals(output, Util.redactYsqlQuery(input));
  }

  @Test
  @Parameters({
    "3ns, 3",
    "2us, 2000",
    "5\u00b5s, 5000",
    "1ms, 1000000",
    "5s, 5000000000",
    "1m, 60000000000",
    "2h, 7200000000000",
    "1d, 86400000000000",
    "1h2m3s, 3723000000000",
    "1ms2us3ns, 1002003"
  })
  public void testGoDurationConversion(String goDuration, long expectedNanos) {
    Duration duration = Util.goDurationToJava(goDuration);
    long nanos = TimeUnit.SECONDS.toNanos(duration.getSeconds()) + duration.getNano();
    assertEquals(expectedNanos, nanos);
  }

  @Test
  @Parameters({
    "kg10, abc",
    "eij0, abcd",
    "n000, 1",
    "tdxk, ReallyReallyLongStringStillHashIs4CharactersOnly"
  })
  public void testBase36hash(String output, String input) {
    assertEquals(output, Util.base36hash(input));
  }
}
