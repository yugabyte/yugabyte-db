// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.api.v2.mappers;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;

import api.v2.mappers.ImageBundleDetailsMapper;
import api.v2.models.ImageBundleDetails;
import com.yugabyte.yw.cloud.PublicCloudConstants.Architecture;
import com.yugabyte.yw.models.ImageBundleDetails.BundleInfo;
import java.util.HashMap;
import java.util.Map;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class ImageBundleDetailsMapperTest {

  @Test
  public void toApi_mapsArchAndRegions() {
    BundleInfo bi = new BundleInfo();
    bi.setYbImage("img");
    Map<String, BundleInfo> regions = new HashMap<>();
    regions.put("us-west-1", bi);

    com.yugabyte.yw.models.ImageBundleDetails src = new com.yugabyte.yw.models.ImageBundleDetails();
    src.setArch(Architecture.x86_64);
    src.setRegions(regions);
    src.setSshUser("centos");
    src.setSshPort(2222);

    ImageBundleDetails out = ImageBundleDetailsMapper.INSTANCE.toApi(src);

    assertEquals(ImageBundleDetails.ArchEnum.X86_64, out.getArch());
    assertEquals("centos", out.getSshUser());
    assertEquals(Integer.valueOf(2222), out.getSshPort());
    assertEquals(1, out.getRegions().size());
    assertEquals("img", out.getRegions().get("us-west-1").getYbImage());
  }

  @Test
  public void toApi_nullArch() {
    com.yugabyte.yw.models.ImageBundleDetails src = new com.yugabyte.yw.models.ImageBundleDetails();
    src.setArch(null);

    ImageBundleDetails out = ImageBundleDetailsMapper.INSTANCE.toApi(src);

    assertNull(out.getArch());
  }
}
