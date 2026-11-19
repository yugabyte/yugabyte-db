// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.api.v2.mappers;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;
import static org.mockito.Mockito.CALLS_REAL_METHODS;
import static org.mockito.Mockito.doReturn;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.mockStatic;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.spy;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import api.v2.mappers.UniverseDefinitionTaskParamsMapper;
import api.v2.models.UniverseInfo;
import api.v2.models.XClusterInfo;
import com.yugabyte.yw.forms.UniverseConfigureTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.helpers.CommonUtils;
import java.util.List;
import java.util.UUID;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.MockedStatic;
import org.mockito.junit.MockitoJUnitRunner;
import play.mvc.Http.Request;

@RunWith(MockitoJUnitRunner.class)
public class UniverseDefinitionTaskParamsMapperTest {
  private static final String SOURCE_ROOT_CERT_DIR = "/mnt/certs";

  @Test
  public void testTaskParamCopySkipsCalculatedXClusterConfigs() {
    UUID sourceConfig = UUID.randomUUID();
    UUID targetConfig = UUID.randomUUID();
    UniverseDefinitionTaskParams source = spy(new UniverseDefinitionTaskParams());
    UniverseDefinitionTaskParams.XClusterInfo xClusterInfo =
        spiedXClusterInfo(sourceConfig, targetConfig);
    source.xClusterInfo = xClusterInfo;

    Request request = mock(Request.class);
    when(request.host()).thenReturn("yba.example");
    UniverseConfigureTaskParams mapped;
    try (MockedStatic<CommonUtils> commonUtils =
        mockStatic(CommonUtils.class, CALLS_REAL_METHODS)) {
      commonUtils.when(CommonUtils::getUserFromContext).thenReturn(null);
      mapped =
          UniverseDefinitionTaskParamsMapper.INSTANCE.toUniverseConfigureTaskParams(
              source, request);
    }

    assertEquals(SOURCE_ROOT_CERT_DIR, mapped.xClusterInfo.sourceRootCertDirPath);
    verify(source, never()).getSourceXClusterConfigs();
    verify(source, never()).getTargetXClusterConfigs();
    verify(xClusterInfo, never()).getSourceXClusterConfigs();
    verify(xClusterInfo, never()).getTargetXClusterConfigs();
  }

  @Test
  public void testV2XClusterInfoSkipsCalculatedConfigs() {
    UUID sourceConfig = UUID.randomUUID();
    UUID targetConfig = UUID.randomUUID();
    UniverseDefinitionTaskParams.XClusterInfo xClusterInfo =
        spiedXClusterInfo(sourceConfig, targetConfig);

    XClusterInfo mapped =
        UniverseDefinitionTaskParamsMapper.INSTANCE.toV2XClusterInfo(xClusterInfo);

    assertEquals(SOURCE_ROOT_CERT_DIR, mapped.getSourceRootCertDirPath());
    assertNull(mapped.getSourceXClusterConfigs());
    assertNull(mapped.getTargetXClusterConfigs());
    verify(xClusterInfo, never()).getSourceXClusterConfigs();
    verify(xClusterInfo, never()).getTargetXClusterConfigs();
  }

  @Test
  public void testUniverseInfoIncludesCalculatedXClusterConfigs() {
    UUID sourceConfig = UUID.randomUUID();
    UUID targetConfig = UUID.randomUUID();
    UniverseDefinitionTaskParams source = new UniverseDefinitionTaskParams();
    source.xClusterInfo = spiedXClusterInfo(sourceConfig, targetConfig);

    UniverseInfo info = UniverseDefinitionTaskParamsMapper.INSTANCE.toV2UniverseInfo(source);

    assertEquals(SOURCE_ROOT_CERT_DIR, info.getxClusterInfo().getSourceRootCertDirPath());
    assertEquals(List.of(sourceConfig), info.getxClusterInfo().getSourceXClusterConfigs());
    assertEquals(List.of(targetConfig), info.getxClusterInfo().getTargetXClusterConfigs());
  }

  private static UniverseDefinitionTaskParams.XClusterInfo spiedXClusterInfo(
      UUID sourceConfig, UUID targetConfig) {
    UniverseDefinitionTaskParams.XClusterInfo xClusterInfo =
        spy(new UniverseDefinitionTaskParams.XClusterInfo());
    xClusterInfo.sourceRootCertDirPath = SOURCE_ROOT_CERT_DIR;
    doReturn(List.of(sourceConfig)).when(xClusterInfo).getSourceXClusterConfigs();
    doReturn(List.of(targetConfig)).when(xClusterInfo).getTargetXClusterConfigs();
    return xClusterInfo;
  }
}
