// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.api.v2.mappers;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;

import api.v2.mappers.ImageBundleMetadataMapper;
import api.v2.models.ImageBundleMetadata;
import com.yugabyte.yw.models.ImageBundle;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class ImageBundleMetadataMapperTest {

  @Test
  public void toApi_mapsTypeAndVersion() {
    ImageBundle.Metadata src = new ImageBundle.Metadata();
    src.setType(ImageBundle.ImageBundleType.CUSTOM);
    src.setVersion("2024.1");

    ImageBundleMetadata out = ImageBundleMetadataMapper.INSTANCE.toApi(src);

    assertEquals(ImageBundleMetadata.TypeEnum.CUSTOM, out.getType());
    assertEquals("2024.1", out.getVersion());
  }

  @Test
  public void toApi_nullType() {
    ImageBundle.Metadata src = new ImageBundle.Metadata();
    src.setType(null);
    src.setVersion("v");

    ImageBundleMetadata out = ImageBundleMetadataMapper.INSTANCE.toApi(src);

    assertNull(out.getType());
    assertEquals("v", out.getVersion());
  }
}
