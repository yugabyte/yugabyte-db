package db.migration.default_.common;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;

import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.gflags.SpecificGFlags;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import io.ebean.DB;
import io.ebean.Transaction;
import java.sql.SQLException;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;
import org.junit.Test;
import play.libs.Json;

@Slf4j
public class V315__Sync_GFlags_From_SpecificGFlagsTest extends FakeDBApplication {
  int i;
  Map<String, String> masterGFlags = Map.of("masterF", "1");
  Map<String, String> tserverGFlags = Map.of("tserverF", "2", "f2", "4");
  Map<String, String> masterGFlagsRR = Map.of("masterFR", "5");
  Map<String, String> tserverGFlagsRR = Map.of("tserverFRR", "6");

  @Test
  public void migrateConfigToDetails() throws SQLException {
    Customer defaultCustomer = ModelFactory.testCustomer();
    List<Universe> universeList = new ArrayList<>();
    // old format
    universeList.add(createWithGFlags(null, masterGFlags, tserverGFlags));
    // old format with rr
    universeList.add(
        createWithGFlags(null, masterGFlags, tserverGFlags, null, masterGFlagsRR, tserverGFlagsRR));
    // new format
    universeList.add(
        createWithGFlags(SpecificGFlags.construct(masterGFlags, tserverGFlags), null, null));
    // new format updated
    universeList.add(
        createWithGFlags(
            SpecificGFlags.construct(masterGFlags, tserverGFlags), masterGFlags, tserverGFlags));
    // new format with rr inherited
    universeList.add(
        createWithGFlags(
            SpecificGFlags.construct(masterGFlags, tserverGFlags),
            null,
            null,
            SpecificGFlags.constructInherited(),
            null,
            null));
    // new format with rr
    universeList.add(
        createWithGFlags(
            SpecificGFlags.construct(masterGFlags, tserverGFlags),
            null,
            null,
            SpecificGFlags.construct(masterGFlagsRR, tserverGFlagsRR),
            null,
            null));

    // ------ Empty universes -------

    Universe empty = createWithGFlags(null, null, null);

    Universe emptyWithInheritedRR =
        createWithGFlags(null, null, null, SpecificGFlags.constructInherited(), null, null);

    Universe emptyWithEmptyRR = createWithGFlags(null, null, null);
    emptyWithEmptyRR =
        Universe.saveDetails(
            emptyWithEmptyRR.getUniverseUUID(),
            ApiUtils.mockUniverseUpdaterWithReadReplica(
                new UniverseDefinitionTaskParams.UserIntent(), null));

    Universe emptyWithNotEmptyRR =
        createWithGFlags(
            null,
            null,
            null,
            SpecificGFlags.construct(masterGFlagsRR, tserverGFlagsRR),
            null,
            null);

    // ------ YBM like --------

    SpecificGFlags specificGFlagsWithAZ = SpecificGFlags.construct(masterGFlags, tserverGFlags);
    specificGFlagsWithAZ.setPerAZ(
        Map.of(
            UUID.randomUUID(),
            SpecificGFlags.construct(Map.of("1", "2"), Map.of("3", "4")).getPerProcessFlags()));
    SpecificGFlags specificGFlagsWithAZRR =
        SpecificGFlags.construct(masterGFlagsRR, tserverGFlagsRR);
    specificGFlagsWithAZRR.setPerAZ(
        Map.of(
            UUID.randomUUID(),
            SpecificGFlags.construct(Map.of("1", "2"), Map.of("3", "4")).getPerProcessFlags()));

    Universe ybmNoRR = createWithGFlags(specificGFlagsWithAZ, masterGFlags, tserverGFlags);

    Universe ybmWithRR =
        createWithGFlags(
            specificGFlagsWithAZ,
            masterGFlags,
            tserverGFlags,
            specificGFlagsWithAZRR,
            masterGFlagsRR,
            tserverGFlagsRR);

    Transaction transaction = DB.beginTransaction();
    try {
      V315__Sync_GFlags_From_SpecificGFlags.migrate(transaction.connection());
      DB.commitTransaction();
    } finally {
      DB.endTransaction();
    }
    verifyUniverses(universeList);
    // Empty
    verifyUniverses(
        Arrays.asList(empty, emptyWithEmptyRR, emptyWithInheritedRR),
        Collections.emptyMap(),
        Collections.emptyMap(),
        Collections.emptyMap(),
        Collections.emptyMap());

    verifyUniverses(
        Arrays.asList(emptyWithNotEmptyRR),
        Collections.emptyMap(),
        Collections.emptyMap(),
        masterGFlagsRR,
        tserverGFlagsRR);

    // YBM Like
    ybmNoRR = Universe.getOrBadRequest(ybmNoRR.getUniverseUUID());
    verify(
        ybmNoRR.getUniverseDetails().getPrimaryCluster(),
        specificGFlagsWithAZ,
        masterGFlags,
        tserverGFlags);

    ybmWithRR = Universe.getOrBadRequest(ybmWithRR.getUniverseUUID());
    verify(
        ybmWithRR.getUniverseDetails().getPrimaryCluster(),
        specificGFlagsWithAZ,
        masterGFlags,
        tserverGFlags);
    verify(
        ybmWithRR.getUniverseDetails().getReadOnlyClusters().get(0),
        specificGFlagsWithAZRR,
        masterGFlagsRR,
        tserverGFlagsRR);
  }

  private void verifyUniverses(List<Universe> universes) {
    verifyUniverses(universes, masterGFlags, tserverGFlags, masterGFlagsRR, tserverGFlagsRR);
  }

  private void verifyUniverses(
      List<Universe> universes,
      Map<String, String> masterGFlags,
      Map<String, String> tserverGFlags,
      Map<String, String> masterGFlagsRR,
      Map<String, String> tserverGFlagsRR) {
    int readonlyCnt = 0;
    for (int i = 0; i < universes.size(); i++) {
      Universe universe = Universe.getOrBadRequest(universes.get(i).getUniverseUUID());
      try {
        UniverseDefinitionTaskParams.Cluster primaryCluster =
            universe.getUniverseDetails().getPrimaryCluster();
        UniverseDefinitionTaskParams.Cluster readonlyCluster =
            universe.getUniverseDetails().clusters.size() > 1
                ? universe.getUniverseDetails().getReadOnlyClusters().get(0)
                : null;

        verify(
            primaryCluster,
            SpecificGFlags.construct(masterGFlags, tserverGFlags),
            masterGFlags,
            tserverGFlags);
        if (readonlyCluster != null) {
          SpecificGFlags readonlySpecificGFlags = readonlyCluster.userIntent.specificGFlags;
          if (readonlySpecificGFlags.isInheritFromPrimary()) {
            assertEquals(masterGFlags, readonlyCluster.userIntent.masterGFlags);
            assertEquals(tserverGFlags, readonlyCluster.userIntent.tserverGFlags);
          } else {
            assertEquals(masterGFlagsRR, readonlyCluster.userIntent.masterGFlags);
            assertEquals(tserverGFlagsRR, readonlyCluster.userIntent.tserverGFlags);
          }
          readonlyCnt++;
        }
      } catch (Throwable e) {
        throw new RuntimeException("Failed to check " + Json.toJson(universe), e);
      }
    }
    assertTrue(readonlyCnt > 0);
  }

  private void verify(
      UniverseDefinitionTaskParams.Cluster cluster,
      SpecificGFlags specificGFlags,
      Map<String, String> masterGFlags,
      Map<String, String> tserverGFlags) {
    assertEquals(masterGFlags, cluster.userIntent.masterGFlags);
    assertEquals(tserverGFlags, cluster.userIntent.tserverGFlags);
    assertEquals(specificGFlags, cluster.userIntent.specificGFlags);
  }

  private Universe createWithGFlags(
      SpecificGFlags specificGFlags,
      Map<String, String> masterGflags,
      Map<String, String> tserverGFlags) {
    return createWithGFlags(specificGFlags, masterGflags, tserverGFlags, null, null, null);
  }

  private Universe createWithGFlags(
      SpecificGFlags specificGFlags,
      Map<String, String> masterGflags,
      Map<String, String> tserverGFlags,
      SpecificGFlags specificGFlagsRR,
      Map<String, String> masterGflagsRR,
      Map<String, String> tserverGFlagsRR) {
    Universe universe = ModelFactory.createUniverse("Universe" + (i++));
    universe =
        Universe.saveDetails(
            universe.getUniverseUUID(),
            u ->
                update(
                    u.getUniverseDetails().getPrimaryCluster().userIntent,
                    specificGFlags,
                    masterGflags,
                    tserverGFlags));
    if (specificGFlagsRR != null || masterGflagsRR != null) {
      UniverseDefinitionTaskParams.UserIntent rrIntent =
          new UniverseDefinitionTaskParams.UserIntent();
      update(rrIntent, specificGFlagsRR, masterGflagsRR, tserverGFlagsRR);
      universe =
          Universe.saveDetails(
              universe.getUniverseUUID(),
              ApiUtils.mockUniverseUpdaterWithReadReplica(rrIntent, null));
    }
    return universe;
  }

  private void update(
      UniverseDefinitionTaskParams.UserIntent userIntent,
      SpecificGFlags specificGFlags,
      Map<String, String> masterGflags,
      Map<String, String> tserverGFlags) {
    userIntent.specificGFlags = specificGFlags;
    userIntent.masterGFlags = masterGflags;
    userIntent.tserverGFlags = tserverGFlags;
  }
}
