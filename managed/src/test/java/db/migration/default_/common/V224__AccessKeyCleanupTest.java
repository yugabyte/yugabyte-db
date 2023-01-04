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
import io.ebean.Ebean;
import io.ebean.SqlUpdate;
import java.util.Collections;
import org.junit.Before;
import org.junit.Test;
import play.libs.Json;

public class V224__AccessKeyCleanupTest extends FakeDBApplication {

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
    AccessKey.create(awsProvider.uuid, "key-1", keyInfoToBeMigrated);

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
    AccessKey accessKey = AccessKey.create(gcpProvider.uuid, "key-2", keyInfoToBeMigrated);
    // this will be needed for fixing the test when the fields are actually deleted
    // from KeyInfo class
    //    updateKeyInfo(accessKey, keyInfoToBeMigrated);
  }

  // this will be needed for keeping the test when the fields are actually deleted from keyInfo
  private static void updateKeyInfo(AccessKey accessKey, KeyInfo info) {
    final SqlUpdate sqlUpdate =
        Ebean.createSqlUpdate(
                "UPDATE access_key SET key_info = :info"
                    + " where key_code = :code and provider_uuid::text = :id")
            .setParameter("info", Json.stringify(Json.toJson(info)))
            .setParameter("code", accessKey.idKey.keyCode)
            .setParameter("id", accessKey.idKey.providerUUID.toString());
    int n = sqlUpdate.execute();
    assertEquals(sqlUpdate.getGeneratedSql(), 1, n);
  }

  @Test
  public void empty() {
    V224__AccessKeyCleanup.migrateAllAccessKeys();
    assertNull(awsProvider.details.sshUser);
    assertEquals(22, awsProvider.details.sshPort.intValue());
    assertFalse(awsProvider.details.airGapInstall);
    assertTrue(awsProvider.details.ntpServers.isEmpty());
    assertFalse(awsProvider.details.setUpChrony);
    assertFalse(awsProvider.details.showSetUpChrony);
    assertTrue(awsProvider.details.passwordlessSudoAccess);
    assertEquals("", awsProvider.details.provisionInstanceScript);
    assertTrue(awsProvider.details.installNodeExporter);
    assertEquals(9300, awsProvider.details.nodeExporterPort.intValue());
    assertEquals("prometheus", awsProvider.details.nodeExporterUser);
    assertFalse(awsProvider.details.skipProvisioning);

    AccessKey accessKey = AccessKey.getLatestKey(awsProvider.uuid);
    assertEquals("PUBLIC_KEY_1234", accessKey.getKeyInfo().publicKey);
    assertEquals("PRIVATE_KEY_5678", accessKey.getKeyInfo().privateKey);
  }

  @Test
  public void nonDefault() {
    V224__AccessKeyCleanup.migrateAllAccessKeys();
    gcpProvider.refresh();
    assertEquals("sbapat", gcpProvider.details.sshUser);
    assertEquals(9022, gcpProvider.details.sshPort.intValue());
    assertTrue(gcpProvider.details.airGapInstall);
    assertEquals(1, gcpProvider.details.ntpServers.size());
    assertEquals("ntp.org", gcpProvider.details.ntpServers.get(0));
    assertTrue(gcpProvider.details.setUpChrony);
    assertTrue(gcpProvider.details.showSetUpChrony);
    assertFalse(gcpProvider.details.passwordlessSudoAccess);
    assertEquals("echo blah", gcpProvider.details.provisionInstanceScript);
    assertFalse(gcpProvider.details.installNodeExporter);
    assertEquals(9500, gcpProvider.details.nodeExporterPort.intValue());
    assertEquals("admin", gcpProvider.details.nodeExporterUser);
    assertTrue(gcpProvider.details.skipProvisioning);

    AccessKey accessKey = AccessKey.getLatestKey(gcpProvider.uuid);
    assertEquals("PUBLIC_KEY_1234", accessKey.getKeyInfo().publicKey);
    assertEquals("PRIVATE_KEY_5678", accessKey.getKeyInfo().privateKey);
  }
}
