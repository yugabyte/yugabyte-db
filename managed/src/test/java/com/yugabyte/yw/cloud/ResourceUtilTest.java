package com.yugabyte.yw.cloud;

import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.models.helpers.DeviceInfo;
import org.junit.Test;

import static org.hamcrest.CoreMatchers.allOf;
import static org.hamcrest.CoreMatchers.equalTo;
import static org.hamcrest.CoreMatchers.notNullValue;
import static org.junit.Assert.assertThat;

public class ResourceUtilTest extends FakeDBApplication {

  @Test
  public void testMergeResourceDetailsInvalidInstanceType() {
    DeviceInfo deviceInfo = new DeviceInfo();
    deviceInfo.numVolumes = 2;
    deviceInfo.volumeSize = 50;
    deviceInfo.numVolumes = 1;
    UniverseResourceDetails details = new UniverseResourceDetails();

    ResourceUtil.mergeResourceDetails(deviceInfo, Common.CloudType.gcp, "m3.fake", "az-1", details);
    assertThat(details.pricePerHour, allOf(notNullValue(), equalTo(0.0)));
  }
}
