// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.api.v2.mappers;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;
import static org.mockito.Mockito.CALLS_REAL_METHODS;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.mockStatic;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.spy;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import api.v2.mappers.UniverseDefinitionTaskParamsMapper;
import api.v2.models.UniverseInfo;
import api.v2.models.XClusterInfo;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.forms.UniverseConfigureTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.XClusterConfig;
import com.yugabyte.yw.models.helpers.CommonUtils;
import java.util.List;
import org.junit.Test;
import org.mockito.MockedStatic;
import org.springframework.test.util.ReflectionTestUtils;
import play.mvc.Http.Request;

public class UniverseDefinitionTaskParamsMapperTest extends FakeDBApplication {
  private static final String SOURCE_ROOT_CERT_DIR = "/mnt/certs";

  @Test
  public void testTaskParamCopySkipsCalculatedXClusterConfigs() {
    UniverseDefinitionTaskParams source = spy(new UniverseDefinitionTaskParams());
    UniverseDefinitionTaskParams.XClusterInfo xClusterInfo = xClusterInfo();
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
    UniverseDefinitionTaskParams.XClusterInfo xClusterInfo = xClusterInfo();

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
    Customer customer = ModelFactory.testCustomer();
    Universe source = ModelFactory.createUniverse("source-universe", customer.getId());
    Universe target = ModelFactory.createUniverse("target-universe", customer.getId());
    XClusterConfig config =
        XClusterConfig.create("repl", source.getUniverseUUID(), target.getUniverseUUID());

    UniverseDefinitionTaskParams details = source.getUniverseDetails();
    details.xClusterInfo.sourceRootCertDirPath = SOURCE_ROOT_CERT_DIR;
    source.setUniverseDetails(details);
    source.save();

    UniverseDefinitionTaskParams reloaded =
        Universe.getOrBadRequest(source.getUniverseUUID()).getUniverseDetails();
    assertNull(ReflectionTestUtils.getField(reloaded.xClusterInfo, "universeUuid"));

    UniverseInfo info = UniverseDefinitionTaskParamsMapper.INSTANCE.toV2UniverseInfo(reloaded);

    assertEquals(SOURCE_ROOT_CERT_DIR, info.getxClusterInfo().getSourceRootCertDirPath());
    assertEquals(List.of(config.getUuid()), info.getxClusterInfo().getSourceXClusterConfigs());
    assertTrue(info.getxClusterInfo().getTargetXClusterConfigs().isEmpty());

    UniverseDefinitionTaskParams targetDetails =
        Universe.getOrBadRequest(target.getUniverseUUID()).getUniverseDetails();
    UniverseInfo targetInfo =
        UniverseDefinitionTaskParamsMapper.INSTANCE.toV2UniverseInfo(targetDetails);
    assertEquals(
        List.of(config.getUuid()), targetInfo.getxClusterInfo().getTargetXClusterConfigs());
    assertTrue(targetInfo.getxClusterInfo().getSourceXClusterConfigs().isEmpty());
  }

  private static UniverseDefinitionTaskParams.XClusterInfo xClusterInfo() {
    UniverseDefinitionTaskParams.XClusterInfo xClusterInfo =
        spy(new UniverseDefinitionTaskParams.XClusterInfo());
    xClusterInfo.sourceRootCertDirPath = SOURCE_ROOT_CERT_DIR;
    return xClusterInfo;
  }
}
