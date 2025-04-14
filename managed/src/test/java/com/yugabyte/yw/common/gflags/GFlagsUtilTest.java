// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.gflags;

import static com.yugabyte.yw.common.gflags.GFlagsUtil.mergeCSVs;
import static org.hamcrest.Matchers.equalTo;
import static org.junit.Assert.*;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.doReturn;

import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.gflags.SpecificGFlags.PerProcessFlags;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import java.io.IOException;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import org.junit.Test;

public class GFlagsUtilTest extends FakeDBApplication {

  @Test
  public void testGflagsAndIntentConsistency() {
    UniverseDefinitionTaskParams.UserIntent userIntent =
        new UniverseDefinitionTaskParams.UserIntent();
    userIntent.enableYSQLAuth = true;
    // Check consistent by default.
    GFlagsUtil.checkGflagsAndIntentConsistency(userIntent);
    userIntent.masterGFlags.put(GFlagsUtil.YSQL_ENABLE_AUTH, "true");
    userIntent.tserverGFlags.put(GFlagsUtil.START_CQL_PROXY, "true");
    userIntent.tserverGFlags.put(GFlagsUtil.USE_CASSANDRA_AUTHENTICATION, "false");
    GFlagsUtil.checkGflagsAndIntentConsistency(userIntent);

    userIntent.tserverGFlags.put(GFlagsUtil.USE_NODE_TO_NODE_ENCRYPTION, "true");
    PlatformServiceException exception =
        assertThrows(
            PlatformServiceException.class,
            () -> GFlagsUtil.checkGflagsAndIntentConsistency(userIntent));
    assertEquals(
        "G-Flag value 'true' for 'use_node_to_node_encryption' is not"
            + " compatible with intent value 'false'",
        exception.getLocalizedMessage());
  }

  @Test
  public void testCheckGFlagsChanged() {
    UniverseDefinitionTaskParams.UserIntent intent = new UniverseDefinitionTaskParams.UserIntent();
    UniverseDefinitionTaskParams.UserIntent intent2 = new UniverseDefinitionTaskParams.UserIntent();
    assertFalse(GFlagsUtil.checkGFlagsByIntentChange(intent, intent2));

    intent.assignPublicIP = !intent.assignPublicIP;
    assertFalse(GFlagsUtil.checkGFlagsByIntentChange(intent, intent2));

    intent2.enableClientToNodeEncrypt = !intent2.enableClientToNodeEncrypt;
    assertTrue(GFlagsUtil.checkGFlagsByIntentChange(intent, intent2));
  }

  @Test
  public void testSyncGflagsToIntent() {
    UniverseDefinitionTaskParams.UserIntent userIntent =
        new UniverseDefinitionTaskParams.UserIntent();
    assertFalse(userIntent.enableNodeToNodeEncrypt);
    assertFalse(userIntent.enableYSQLAuth);
    assertTrue(userIntent.enableYEDIS);
    GFlagsUtil.syncGflagsToIntent(
        ImmutableMap.of(GFlagsUtil.USE_NODE_TO_NODE_ENCRYPTION, "false"), userIntent);
    assertFalse(userIntent.enableNodeToNodeEncrypt);
    GFlagsUtil.syncGflagsToIntent(
        ImmutableMap.of(
            GFlagsUtil.USE_NODE_TO_NODE_ENCRYPTION,
            "true",
            GFlagsUtil.YSQL_ENABLE_AUTH,
            "true",
            GFlagsUtil.START_REDIS_PROXY,
            "false"),
        userIntent);
    assertTrue(userIntent.enableNodeToNodeEncrypt);
    assertTrue(userIntent.enableYSQLAuth);
    assertFalse(userIntent.enableYEDIS);
  }

  @Test
  public void testCheckConsistency() {
    Map<String, String> master = ImmutableMap.of("flag1", "1", "flag2", "2");
    Map<String, String> tserver = ImmutableMap.of("flag1", "2", "flag2", "2", "flag3", "abc");
    // Check other values.
    GFlagsUtil.checkConsistency(master, tserver);

    // Check same values.
    GFlagsUtil.checkConsistency(
        ImmutableMap.of(GFlagsUtil.ENABLE_YSQL, "true", "gflag1", "5"),
        ImmutableMap.of(GFlagsUtil.ENABLE_YSQL, "true"));

    // Check with no intersection in flags.
    GFlagsUtil.checkConsistency(
        ImmutableMap.of(GFlagsUtil.ENABLE_YSQL, "true"),
        ImmutableMap.of(GFlagsUtil.START_CQL_PROXY, "true"));

    PlatformServiceException exception =
        assertThrows(
            PlatformServiceException.class,
            () ->
                GFlagsUtil.checkConsistency(
                    ImmutableMap.of("gflag1", "1", GFlagsUtil.START_CQL_PROXY, "true"),
                    ImmutableMap.of(GFlagsUtil.START_CQL_PROXY, "false")));
    assertEquals(
        "G-Flag value for 'start_cql_proxy' is inconsistent between "
            + "master and tserver ('true' vs 'false')",
        exception.getLocalizedMessage());
  }

  @Test
  public void testMergeCsv() {
    String csv1 = "key1=val1,key2=val2,qwe";
    String csv2 = "key1=val11,key3=val3,qwe asd";

    String mergeNoKeyValues = mergeCSVs(csv1, csv2, false);
    assertThat(mergeNoKeyValues, equalTo("key1=val1,key2=val2,qwe,key1=val11,key3=val3,qwe asd"));

    String mergeKeyValues = mergeCSVs(csv1, csv2, true);
    assertThat(mergeKeyValues, equalTo("key1=val1,key2=val2,qwe,key3=val3,qwe asd"));
  }

  @Test
  public void testProcessGFlagGroups() throws IOException {

    List<GFlagGroup> gFlagGroups = new ArrayList<>();
    GFlagGroup gFlagGroup = new GFlagGroup();
    gFlagGroup.groupName = GFlagGroup.GroupName.ENHANCED_POSTGRES_COMPATIBILITY;
    GFlagGroup.ServerTypeFlags flags = new GFlagGroup.ServerTypeFlags();
    flags.masterGFlags = new HashMap<>();
    flags.tserverGFlags = new HashMap<>();
    flags.tserverGFlags.put("yb_enable_read_committed_isolation", "true");
    flags.tserverGFlags.put("ysql_enable_read_request_caching", "true");
    flags.tserverGFlags.put(
        "ysql_pg_conf_csv",
        "yb_enable_base_scans_cost_model=true,"
            + "yb_enable_optimizer_statistics=true,"
            + "yb_bnl_batch_size=1024,"
            + "yb_parallel_range_rows=10000,"
            + "yb_fetch_row_limit=0,"
            + "yb_fetch_size_limit='1MB',"
            + "yb_use_hash_splitting_by_default=false");

    gFlagGroup.flags = flags;
    gFlagGroups.add(gFlagGroup);
    doReturn(gFlagGroups).when(mockGFlagsValidation).extractGFlagGroups(any());

    SpecificGFlags specificGFlags1 = new SpecificGFlags();
    List<GFlagGroup.GroupName> gflagGroups = new ArrayList<>();
    gflagGroups.add(GFlagGroup.GroupName.ENHANCED_POSTGRES_COMPATIBILITY);
    specificGFlags1.setGflagGroups(gflagGroups);

    Map<String, String> master = new HashMap<>();
    master.put("flag1", "1");
    master.put("flag2", "2");
    Map<String, String> tserver = new HashMap<>();
    tserver.put("flag1", "2");
    tserver.put("flag2", "2");
    tserver.put("flag3", "abc");

    SpecificGFlags.PerProcessFlags perProcessFlags = new SpecificGFlags.PerProcessFlags();
    perProcessFlags.value = new HashMap<>();
    perProcessFlags.value.put(ServerType.TSERVER, new HashMap<>(tserver));
    perProcessFlags.value.put(ServerType.MASTER, new HashMap<>(master));
    specificGFlags1.setPerProcessFlags(perProcessFlags);

    UniverseDefinitionTaskParams.UserIntent userIntent1 =
        new UniverseDefinitionTaskParams.UserIntent();

    userIntent1.specificGFlags = specificGFlags1;

    // Test with lower DB version
    userIntent1.ybSoftwareVersion = "2024.0.0.0";
    PlatformServiceException exception1 =
        assertThrows(
            PlatformServiceException.class,
            () -> GFlagsUtil.processGFlagGroups(master, userIntent1, ServerType.MASTER));

    // Test with correct DB version
    userIntent1.ybSoftwareVersion = "2.23.0.0-b417";
    GFlagsUtil.processGFlagGroups(master, userIntent1, ServerType.MASTER);
    assertThat(master, equalTo(ImmutableMap.of("flag1", "1", "flag2", "2")));

    GFlagsUtil.processGFlagGroups(tserver, userIntent1, ServerType.TSERVER);
    assertThat(
        tserver,
        equalTo(
            ImmutableMap.of(
                "flag1",
                "2",
                "flag2",
                "2",
                "flag3",
                "abc",
                "yb_enable_read_committed_isolation",
                "true",
                "ysql_enable_read_request_caching",
                "true",
                "ysql_pg_conf_csv",
                "yb_enable_base_scans_cost_model=true,"
                    + "yb_enable_optimizer_statistics=true,"
                    + "yb_bnl_batch_size=1024,"
                    + "yb_parallel_range_rows=10000,"
                    + "yb_fetch_row_limit=0,"
                    + "yb_fetch_size_limit='1MB',"
                    + "yb_use_hash_splitting_by_default=false")));

    Map<String, String> tserver1 = new HashMap<>();
    tserver1.put("flag1", "2");
    tserver1.put("flag2", "2");
    tserver1.put("flag3", "abc");
    tserver1.put("ysql_pg_conf_csv", "abc=def");
    perProcessFlags.value.put(ServerType.TSERVER, new HashMap<>(tserver1));
    specificGFlags1.setPerProcessFlags(perProcessFlags);
    userIntent1.specificGFlags = specificGFlags1;

    GFlagsUtil.processGFlagGroups(tserver1, userIntent1, ServerType.TSERVER);
    assertThat(
        tserver1,
        equalTo(
            ImmutableMap.of(
                "flag1",
                "2",
                "flag2",
                "2",
                "flag3",
                "abc",
                "yb_enable_read_committed_isolation",
                "true",
                "ysql_enable_read_request_caching",
                "true",
                "ysql_pg_conf_csv",
                "yb_enable_base_scans_cost_model=true,"
                    + "yb_enable_optimizer_statistics=true,"
                    + "yb_bnl_batch_size=1024,"
                    + "yb_parallel_range_rows=10000,"
                    + "yb_fetch_row_limit=0,"
                    + "yb_fetch_size_limit='1MB',"
                    + "yb_use_hash_splitting_by_default=false,"
                    + "abc=def")));

    Map<String, String> tserver2 = new HashMap<>();
    tserver2.put("flag1", "2");
    tserver2.put("flag2", "2");
    tserver2.put("flag3", "abc");
    tserver2.put("ysql_pg_conf_csv", "abc=def,yb_enable_base_scans_cost_model=false");
    tserver2.put("ysql_enable_read_request_caching", "false");
    perProcessFlags.value.put(ServerType.TSERVER, new HashMap<>(tserver2));
    specificGFlags1.setPerProcessFlags(perProcessFlags);
    userIntent1.specificGFlags = specificGFlags1;

    GFlagsUtil.processGFlagGroups(tserver2, userIntent1, ServerType.TSERVER);
    assertThat(
        tserver2,
        equalTo(
            ImmutableMap.of(
                "flag1",
                "2",
                "flag2",
                "2",
                "flag3",
                "abc",
                "yb_enable_read_committed_isolation",
                "true",
                "ysql_enable_read_request_caching",
                "true",
                "ysql_pg_conf_csv",
                "yb_enable_base_scans_cost_model=true,"
                    + "yb_enable_optimizer_statistics=true,"
                    + "yb_bnl_batch_size=1024,"
                    + "yb_parallel_range_rows=10000,"
                    + "yb_fetch_row_limit=0,"
                    + "yb_fetch_size_limit='1MB',"
                    + "yb_use_hash_splitting_by_default=false,"
                    + "abc=def")));

    Map<String, String> tserver3 = new HashMap<>();
    tserver3.put("flag1", "2");
    tserver3.put("flag2", "2");
    tserver3.put("flag3", "abc");
    tserver3.put("ysql_pg_conf_csv", "abc=def");
    perProcessFlags.value.put(ServerType.TSERVER, new HashMap<>(tserver3));
    specificGFlags1.setPerProcessFlags(perProcessFlags);
    gflagGroups = new ArrayList<>();
    specificGFlags1.setGflagGroups(gflagGroups);
    userIntent1.specificGFlags = specificGFlags1;

    GFlagsUtil.processGFlagGroups(tserver, userIntent1, ServerType.TSERVER);
    assertThat(
        tserver3,
        equalTo(
            ImmutableMap.of(
                "flag1", "2", "flag2", "2", "flag3", "abc", "ysql_pg_conf_csv", "abc=def")));
  }

  @Test
  public void testSpecificGflagsMerge() {
    // Create the base specific gflags.
    SpecificGFlags baseSpecificGFlags = new SpecificGFlags();

    // Add gflag groups.
    List<GFlagGroup.GroupName> gflagGroups1 = new ArrayList<>();
    // gflagGroups1.add(GFlagGroup.GroupName.ENHANCED_POSTGRES_COMPATIBILITY);
    baseSpecificGFlags.setGflagGroups(gflagGroups1);

    // Add per process flags.
    Map<String, String> master1 = new HashMap<>();
    master1.put("flag1", "1");
    master1.put("flag2", "2");
    // Map<String, String> tserver1 = new HashMap<>();
    SpecificGFlags.PerProcessFlags perProcessFlags1 = new SpecificGFlags.PerProcessFlags();
    perProcessFlags1.value = new HashMap<>();
    perProcessFlags1.value.put(ServerType.MASTER, new HashMap<>(master1));
    baseSpecificGFlags.setPerProcessFlags(perProcessFlags1);

    // Add per az flags.
    UUID azUUID1 = UUID.randomUUID();

    Map<UUID, PerProcessFlags> perAzFlags1 = new HashMap<>();
    perAzFlags1.put(azUUID1, perProcessFlags1);
    baseSpecificGFlags.setPerAZ(perAzFlags1);

    // Create the extra specific gflags.
    SpecificGFlags extraSpecificGFlags = new SpecificGFlags();

    // Add gflag groups.
    List<GFlagGroup.GroupName> gflagGroups2 = new ArrayList<>();
    gflagGroups2.add(GFlagGroup.GroupName.ENHANCED_POSTGRES_COMPATIBILITY);
    extraSpecificGFlags.setGflagGroups(gflagGroups2);

    // Add per process flags.
    Map<String, String> master2 = new HashMap<>();
    master2.put("flag2", "a");
    master2.put("flag3", "b");
    master2.put("flag4", "c");
    Map<String, String> tserver2 = new HashMap<>();
    tserver2.put("flag3", "d");
    tserver2.put("flag4", "e");
    tserver2.put("flag5", "f");
    SpecificGFlags.PerProcessFlags perProcessFlags2 = new SpecificGFlags.PerProcessFlags();
    perProcessFlags2.value = new HashMap<>();
    perProcessFlags2.value.put(ServerType.MASTER, new HashMap<>(master2));
    perProcessFlags2.value.put(ServerType.TSERVER, new HashMap<>(tserver2));
    extraSpecificGFlags.setPerProcessFlags(perProcessFlags2);

    // Add per az flags.
    UUID azUUID2 = UUID.randomUUID();

    Map<UUID, PerProcessFlags> perAzFlags2 = new HashMap<>();
    perAzFlags2.put(azUUID1, perProcessFlags2);
    perAzFlags2.put(azUUID2, perProcessFlags1);
    extraSpecificGFlags.setPerAZ(perAzFlags2);

    // Combine and check.
    SpecificGFlags finalSpecificGFlags =
        SpecificGFlags.combine(baseSpecificGFlags, extraSpecificGFlags);

    // Validate the gflag groups.
    assertEquals(baseSpecificGFlags.getGflagGroups().size(), 0);
    assertEquals(extraSpecificGFlags.getGflagGroups().size(), 1);
    assertEquals(finalSpecificGFlags.getGflagGroups().size(), 1);
    assertEquals(
        finalSpecificGFlags.getGflagGroups().get(0),
        GFlagGroup.GroupName.ENHANCED_POSTGRES_COMPATIBILITY);

    // Validate the per process groups.
    // Check that the base flags don't change.
    assertEquals(baseSpecificGFlags.getPerProcessFlags().value.size(), 1);
    assertEquals(baseSpecificGFlags.getPerProcessFlags().value.get(ServerType.MASTER).size(), 2);
    // Check that the extra flags don't change.
    assertEquals(extraSpecificGFlags.getPerProcessFlags().value.size(), 2);
    assertEquals(extraSpecificGFlags.getPerProcessFlags().value.get(ServerType.MASTER).size(), 3);
    assertEquals(extraSpecificGFlags.getPerProcessFlags().value.get(ServerType.TSERVER).size(), 3);
    // Check the final per process master flags.
    assertEquals(finalSpecificGFlags.getPerProcessFlags().value.size(), 2);
    assertEquals(finalSpecificGFlags.getPerProcessFlags().value.get(ServerType.MASTER).size(), 4);
    assertEquals(
        finalSpecificGFlags.getPerProcessFlags().value.get(ServerType.MASTER).get("flag1"), "1");
    assertEquals(
        finalSpecificGFlags.getPerProcessFlags().value.get(ServerType.MASTER).get("flag2"), "a");
    assertEquals(
        finalSpecificGFlags.getPerProcessFlags().value.get(ServerType.MASTER).get("flag3"), "b");
    assertEquals(
        finalSpecificGFlags.getPerProcessFlags().value.get(ServerType.MASTER).get("flag4"), "c");
    // Check the final per process tserver flags.
    assertEquals(finalSpecificGFlags.getPerProcessFlags().value.get(ServerType.TSERVER).size(), 3);
    assertEquals(
        finalSpecificGFlags.getPerProcessFlags().value.get(ServerType.TSERVER).get("flag3"), "d");
    assertEquals(
        finalSpecificGFlags.getPerProcessFlags().value.get(ServerType.TSERVER).get("flag4"), "e");
    assertEquals(
        finalSpecificGFlags.getPerProcessFlags().value.get(ServerType.TSERVER).get("flag5"), "f");

    // Validate the per az flags.
    // Check that the base flags don't change.
    assertEquals(baseSpecificGFlags.getPerAZ().size(), 1);
    assertEquals(baseSpecificGFlags.getPerAZ().get(azUUID1).value.size(), 1);
    assertEquals(baseSpecificGFlags.getPerAZ().get(azUUID1).value.get(ServerType.MASTER).size(), 2);

    // Check that the extra flags don't change.
    assertEquals(extraSpecificGFlags.getPerAZ().size(), 2);
    // Check that the az1 flags are same.
    assertEquals(extraSpecificGFlags.getPerAZ().get(azUUID1).value.size(), 2);
    assertEquals(
        extraSpecificGFlags.getPerAZ().get(azUUID1).value.get(ServerType.MASTER).size(), 3);
    assertEquals(
        extraSpecificGFlags.getPerAZ().get(azUUID1).value.get(ServerType.TSERVER).size(), 3);
    // Check that the az2 flags are same.
    assertEquals(extraSpecificGFlags.getPerAZ().get(azUUID2).value.size(), 1);
    assertEquals(
        extraSpecificGFlags.getPerAZ().get(azUUID2).value.get(ServerType.MASTER).size(), 2);

    // Check the final gflags.
    assertEquals(finalSpecificGFlags.getPerAZ().size(), 2);
    // Check the az1 master gflags.
    assertEquals(finalSpecificGFlags.getPerAZ().get(azUUID1).value.size(), 2);
    assertEquals(
        finalSpecificGFlags.getPerAZ().get(azUUID1).value.get(ServerType.MASTER).size(), 4);
    assertEquals(
        finalSpecificGFlags.getPerAZ().get(azUUID1).value.get(ServerType.MASTER).get("flag1"), "1");
    assertEquals(
        finalSpecificGFlags.getPerAZ().get(azUUID1).value.get(ServerType.MASTER).get("flag2"), "a");
    assertEquals(
        finalSpecificGFlags.getPerAZ().get(azUUID1).value.get(ServerType.MASTER).get("flag3"), "b");
    assertEquals(
        finalSpecificGFlags.getPerAZ().get(azUUID1).value.get(ServerType.MASTER).get("flag4"), "c");
    // Check the az1 tserver gflags.
    assertEquals(
        finalSpecificGFlags.getPerAZ().get(azUUID1).value.get(ServerType.TSERVER).size(), 3);
    assertEquals(
        finalSpecificGFlags.getPerAZ().get(azUUID1).value.get(ServerType.TSERVER).get("flag3"),
        "d");
    assertEquals(
        finalSpecificGFlags.getPerAZ().get(azUUID1).value.get(ServerType.TSERVER).get("flag4"),
        "e");
    assertEquals(
        finalSpecificGFlags.getPerAZ().get(azUUID1).value.get(ServerType.TSERVER).get("flag5"),
        "f");
    // Check the az2 master gflags.
    assertEquals(finalSpecificGFlags.getPerAZ().get(azUUID2).value.size(), 1);
    assertEquals(
        finalSpecificGFlags.getPerAZ().get(azUUID2).value.get(ServerType.MASTER).size(), 2);
    assertEquals(
        finalSpecificGFlags.getPerAZ().get(azUUID2).value.get(ServerType.MASTER).get("flag1"), "1");
    assertEquals(
        finalSpecificGFlags.getPerAZ().get(azUUID2).value.get(ServerType.MASTER).get("flag2"), "2");
    // Check the az2 tserver gflags not present.
    assertFalse(finalSpecificGFlags.getPerAZ().get(azUUID2).value.containsKey(ServerType.TSERVER));

    // This checks the number of fields present in the SpecificGflags class. If this number
    // increases, then the developer needs to accomodate that in the `SpecificGFlags.combine()`
    // function, update this UT, and increase this count here. This is just so that we don't miss
    // combining the objects in the future when we add new child fields to this class.
    assertEquals(finalSpecificGFlags.getClass().getDeclaredFields().length, 4);
  }
}
