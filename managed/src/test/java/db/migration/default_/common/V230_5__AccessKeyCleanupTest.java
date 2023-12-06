/*
 * Copyright 2022 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package db.migration.default_.common;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;

import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.models.AccessKey;
import com.yugabyte.yw.models.AccessKey.KeyInfo;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;
import io.ebean.DB;
import io.ebean.SqlUpdate;
import java.util.Collections;
import junitparams.JUnitParamsRunner;
import junitparams.Parameters;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import play.libs.Json;

@RunWith(JUnitParamsRunner.class)
public class V230_5__AccessKeyCleanupTest extends FakeDBApplication {

  // TODO make the test use tmp entities if this proves to be a problem.
  private Provider awsProvider;
  private Provider gcpProvider;

  @Before
  public void setup() {
    Customer defaultCustomer = ModelFactory.testCustomer();

    // Create provider and key with empty values for fields to be migrated
    awsProvider = ModelFactory.awsProvider(defaultCustomer);
    KeyInfo keyInfoToBeMigrated = new KeyInfo();
    keyInfoToBeMigrated.publicKey = "PUBLIC_KEY_1234";
    keyInfoToBeMigrated.privateKey = "PRIVATE_KEY_5678";
    AccessKey.create(awsProvider.getUuid(), "key-1", keyInfoToBeMigrated);

    // Create another provider and key with non-default values for fields to be migrated
    gcpProvider = ModelFactory.gcpProvider(defaultCustomer);
    keyInfoToBeMigrated.sshUser = "sbapat";
    keyInfoToBeMigrated.sshPort = 9022;
    keyInfoToBeMigrated.airGapInstall = true;
    keyInfoToBeMigrated.ntpServers = Collections.singletonList("ntp.org");
    keyInfoToBeMigrated.setUpChrony = true;
    keyInfoToBeMigrated.showSetUpChrony = true;
    keyInfoToBeMigrated.passwordlessSudoAccess = false;
    keyInfoToBeMigrated.provisionInstanceScript = "echo blah";
    keyInfoToBeMigrated.installNodeExporter = false;
    keyInfoToBeMigrated.nodeExporterPort = 9500;
    keyInfoToBeMigrated.nodeExporterUser = "admin";
    keyInfoToBeMigrated.skipProvisioning = true;
    AccessKey accessKey = AccessKey.create(gcpProvider.getUuid(), "key-2", keyInfoToBeMigrated);
    // this will be needed for fixing the test when the fields are actually deleted
    // from KeyInfo class
    //    updateKeyInfo(accessKey, keyInfoToBeMigrated);
  }

  // this will be needed for keeping the test when the fields are actually deleted from keyInfo
  private static void updateKeyInfo(AccessKey accessKey, KeyInfo info) {
    final SqlUpdate sqlUpdate =
        DB.sqlUpdate(
                "UPDATE access_key SET key_info = :info"
                    + " where key_code = :code and provider_uuid::text = :id")
            .setParameter("info", Json.stringify(Json.toJson(info)))
            .setParameter("code", accessKey.getIdKey().keyCode)
            .setParameter("id", accessKey.getIdKey().providerUUID.toString());
    int n = sqlUpdate.execute();
    assertEquals(sqlUpdate.getGeneratedSql(), 1, n);
  }

  @Test
  @Parameters({"true", "false"})
  public void empty(boolean isOldProvider) {
    if (isOldProvider) {
      awsProvider.setDetails(null);
      awsProvider.save();
    }
    V230_5__AccessKeyCleanup.migrateAllAccessKeys();
    awsProvider.refresh();
    assertNull(awsProvider.getDetails().sshUser);
    assertEquals(22, awsProvider.getDetails().sshPort.intValue());
    assertFalse(awsProvider.getDetails().airGapInstall);
    assertTrue(awsProvider.getDetails().ntpServers.isEmpty());
    assertFalse(awsProvider.getDetails().setUpChrony);
    assertFalse(awsProvider.getDetails().showSetUpChrony);
    assertTrue(awsProvider.getDetails().passwordlessSudoAccess);
    assertEquals("", awsProvider.getDetails().provisionInstanceScript);
    assertTrue(awsProvider.getDetails().installNodeExporter);
    assertEquals(9300, awsProvider.getDetails().nodeExporterPort.intValue());
    assertEquals("prometheus", awsProvider.getDetails().nodeExporterUser);
    assertFalse(awsProvider.getDetails().skipProvisioning);

    AccessKey accessKey = AccessKey.getLatestKey(awsProvider.getUuid());
    assertEquals("PUBLIC_KEY_1234", accessKey.getKeyInfo().publicKey);
    assertEquals("PRIVATE_KEY_5678", accessKey.getKeyInfo().privateKey);
  }

  @Test
  public void plat_8027() {
    awsProvider.getDetails().sshPort = 54422;
    awsProvider.save();
    V230_5__AccessKeyCleanup.migrateAllAccessKeys();
    awsProvider.refresh();
    assertNull(awsProvider.getDetails().sshUser);
    assertEquals(54422, awsProvider.getDetails().sshPort.intValue());
  }

  @Test
  @Parameters({"true", "false"})
  public void nonDefault(boolean isOldProvider) {
    if (isOldProvider) {
      gcpProvider.setDetails(null);
    }
    V230_5__AccessKeyCleanup.migrateAllAccessKeys();
    gcpProvider.refresh();
    assertEquals("sbapat", gcpProvider.getDetails().sshUser);
    assertEquals(9022, gcpProvider.getDetails().sshPort.intValue());
    assertTrue(gcpProvider.getDetails().airGapInstall);
    assertEquals(1, gcpProvider.getDetails().ntpServers.size());
    assertEquals("ntp.org", gcpProvider.getDetails().ntpServers.get(0));
    assertTrue(gcpProvider.getDetails().setUpChrony);
    assertTrue(gcpProvider.getDetails().showSetUpChrony);
    assertFalse(gcpProvider.getDetails().passwordlessSudoAccess);
    assertEquals("echo blah", gcpProvider.getDetails().provisionInstanceScript);
    assertFalse(gcpProvider.getDetails().installNodeExporter);
    assertEquals(9500, gcpProvider.getDetails().nodeExporterPort.intValue());
    assertEquals("admin", gcpProvider.getDetails().nodeExporterUser);
    assertTrue(gcpProvider.getDetails().skipProvisioning);

    AccessKey accessKey = AccessKey.getLatestKey(gcpProvider.getUuid());
    assertEquals("PUBLIC_KEY_1234", accessKey.getKeyInfo().publicKey);
    assertEquals("PRIVATE_KEY_5678", accessKey.getKeyInfo().privateKey);
  }
}
