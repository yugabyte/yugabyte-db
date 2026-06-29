// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.api.v2.mappers;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;

import api.v2.mappers.ImageBundleRegionInfoMapper;
import api.v2.models.ImageBundleRegionInfo;
import com.yugabyte.yw.models.ImageBundleDetails;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class ImageBundleRegionInfoMapperTest {

  @Test
  public void toApi_mapsRegionBundleFields() {
    ImageBundleDetails.BundleInfo src = new ImageBundleDetails.BundleInfo();
    src.setYbImage("ami-123");

    ImageBundleRegionInfo out = ImageBundleRegionInfoMapper.INSTANCE.toApi(src);

    assertEquals("ami-123", out.getYbImage());
  }

  @Test
  public void toApi_nullSource() {
    assertNull(ImageBundleRegionInfoMapper.INSTANCE.toApi(null));
  }
}
