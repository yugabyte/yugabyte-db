// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.api.v2.mappers;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertTrue;
import static org.mockito.Mockito.mockStatic;

import api.v2.mappers.KmsConfigurationMapper;
import api.v2.models.KmsConfiguration;
import api.v2.models.KmsConfigurationInfo;
import api.v2.models.KmsConfigurationSpec;
import com.yugabyte.yw.common.kms.util.EncryptionAtRestUtil;
import com.yugabyte.yw.common.kms.util.KeyProvider;
import com.yugabyte.yw.models.KmsConfig;
import java.util.Collections;
import java.util.UUID;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.MockedStatic;
import org.mockito.junit.MockitoJUnitRunner;
import play.libs.Json;

@RunWith(MockitoJUnitRunner.class)
public class KmsConfigurationMapperTest {

  @Test
  public void toKmsConfiguration_mapsSpecAndInfoWhenCredentialsPresent() {
    UUID configUuid = UUID.randomUUID();
    KmsConfig config = new KmsConfig();
    config.setConfigUUID(configUuid);
    config.setName("kms-row");
    config.setKeyProvider(KeyProvider.AWS);
    config.setVersion(KmsConfig.SCHEMA_VERSION);
    config.setAuthConfig(Json.newObject().put("ACCESS_KEY", "secret"));

    try (MockedStatic<EncryptionAtRestUtil> util = mockStatic(EncryptionAtRestUtil.class)) {
      util.when(() -> EncryptionAtRestUtil.configInUse(configUuid)).thenReturn(true);
      util.when(() -> EncryptionAtRestUtil.getUniverses(configUuid))
          .thenReturn(Collections.emptyList());

      KmsConfiguration out = KmsConfigurationMapper.INSTANCE.toKmsConfiguration(config);

      assertNotNull(out.getSpec());
      assertNotNull(out.getInfo());
      KmsConfigurationSpec spec = out.getSpec();
      KmsConfigurationInfo info = out.getInfo();

      assertNotNull(spec.getCredentials());
      assertTrue(spec.getCredentials().containsKey("ACCESS_KEY"));
      assertEquals("kms-row", spec.getName());
      assertEquals("AWS", spec.getProvider());

      assertEquals(configUuid, info.getUuid());
      assertTrue(info.getInUse());
      assertTrue(info.getUniverseDetails().isEmpty());
    }
  }

  @Test
  public void toKmsConfiguration_emptyCredentialsWhenAuthConfigNull() {
    UUID configUuid = UUID.randomUUID();
    KmsConfig config = new KmsConfig();
    config.setConfigUUID(configUuid);
    config.setName("x");
    config.setKeyProvider(KeyProvider.AWS);
    config.setVersion(KmsConfig.SCHEMA_VERSION);
    config.setAuthConfig(null);

    try (MockedStatic<EncryptionAtRestUtil> util = mockStatic(EncryptionAtRestUtil.class)) {
      util.when(() -> EncryptionAtRestUtil.configInUse(configUuid)).thenReturn(false);
      util.when(() -> EncryptionAtRestUtil.getUniverses(configUuid))
          .thenReturn(Collections.emptyList());

      KmsConfiguration out = KmsConfigurationMapper.INSTANCE.toKmsConfiguration(config);

      assertNotNull(out.getSpec());
      assertTrue(out.getSpec().getCredentials().isEmpty());
      assertEquals("x", out.getSpec().getName());
      assertEquals(false, out.getInfo().getInUse());
    }
  }
}
