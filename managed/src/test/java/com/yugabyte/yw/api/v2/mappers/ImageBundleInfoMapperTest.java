// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.api.v2.mappers;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;

import api.v2.mappers.ImageBundleInfoMapper;
import api.v2.models.ImageBundleInfo;
import com.yugabyte.yw.models.ImageBundleDetails;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class ImageBundleInfoMapperTest {

  @Test
  public void toApi_mapsRegionBundleFields() {
    ImageBundleDetails.BundleInfo src = new ImageBundleDetails.BundleInfo();
    src.setYbImage("ami-123");

    ImageBundleInfo out = ImageBundleInfoMapper.INSTANCE.toApi(src);

    assertEquals("ami-123", out.getYbImage());
  }

  @Test
  public void toApi_nullSource() {
    assertNull(ImageBundleInfoMapper.INSTANCE.toApi(null));
  }
}
