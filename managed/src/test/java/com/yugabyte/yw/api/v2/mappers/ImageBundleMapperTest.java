// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.api.v2.mappers;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertTrue;

import api.v2.mappers.ImageBundleMapper;
import api.v2.models.ImageBundle;
import api.v2.models.ImageBundleInfo;
import api.v2.models.ImageBundleMetadata;
import api.v2.models.ImageBundleSpec;
import com.yugabyte.yw.cloud.PublicCloudConstants.Architecture;
import java.util.HashMap;
import java.util.UUID;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class ImageBundleMapperTest {

  @Test
  public void toApi_mapsSpecAndInfo() {
    UUID id = UUID.randomUUID();
    com.yugabyte.yw.models.ImageBundleDetails details =
        new com.yugabyte.yw.models.ImageBundleDetails();
    details.setArch(Architecture.aarch64);
    details.setRegions(new HashMap<>());

    com.yugabyte.yw.models.ImageBundle.Metadata meta =
        new com.yugabyte.yw.models.ImageBundle.Metadata();
    meta.setType(com.yugabyte.yw.models.ImageBundle.ImageBundleType.YBA_ACTIVE);
    meta.setVersion("2.0");

    com.yugabyte.yw.models.ImageBundle src = new com.yugabyte.yw.models.ImageBundle();
    src.setUuid(id);
    src.setName("ib-name");
    src.setDetails(details);
    src.setMetadata(meta);
    src.setActive(false);
    src.setUseAsDefault(true);

    ImageBundle out = ImageBundleMapper.INSTANCE.toApi(src);

    assertNotNull(out.getSpec());
    assertNotNull(out.getInfo());
    ImageBundleSpec spec = out.getSpec();
    ImageBundleInfo info = out.getInfo();

    assertEquals("ib-name", spec.getName());
    assertEquals(api.v2.models.ImageBundleDetails.ArchEnum.AARCH64, spec.getDetails().getArch());
    assertTrue(spec.getUseAsDefault());

    assertEquals(id, info.getUuid());
    assertEquals(ImageBundleMetadata.TypeEnum.YBA_ACTIVE, info.getMetadata().getType());
    assertEquals("2.0", info.getMetadata().getVersion());
    assertFalse(info.getActive());
  }
}
