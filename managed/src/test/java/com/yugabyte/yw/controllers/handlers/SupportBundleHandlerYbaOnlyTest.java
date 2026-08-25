// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.controllers.handlers;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;
import static play.mvc.Http.Status.BAD_REQUEST;

import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.forms.SupportBundleFormDataV2;
import com.yugabyte.yw.models.helpers.BundleDetails.ComponentType;
import com.yugabyte.yw.models.helpers.BundleDetails.YbaComponentSpec;
import java.util.EnumSet;
import java.util.List;
import org.junit.Before;
import org.junit.Test;

public class SupportBundleHandlerYbaOnlyTest extends FakeDBApplication {

  private SupportBundleHandlerV2 handler;

  @Before
  public void setUp() {
    handler = app.injector().instanceOf(SupportBundleHandlerV2.class);
  }

  @Test
  public void testIsYbaComponentOnly() {
    SupportBundleFormDataV2 bundleData = new SupportBundleFormDataV2();
    bundleData.components = EnumSet.of(ComponentType.YBAComponent);
    assertTrue(bundleData.isYbaComponentOnly());

    bundleData.components = EnumSet.of(ComponentType.YBAComponent, ComponentType.FilesComponent);
    assertFalse(bundleData.isYbaComponentOnly());
  }

  private static SupportBundleFormDataV2 ybaOnlyBundleData(String entrypoint) {
    SupportBundleFormDataV2 bundleData = new SupportBundleFormDataV2();
    bundleData.components = EnumSet.of(ComponentType.YBAComponent);
    YbaComponentSpec spec = new YbaComponentSpec();
    spec.setComponentName("ApplicationLogs");
    spec.setScriptPath("bin/yba_utils.sh");
    spec.setParams(List.of(entrypoint, "--remote_tar_path", "/tmp/custom-yba-script.tar.gz"));
    spec.setRemoteTarPath("/tmp/custom-yba-script.tar.gz");
    bundleData.ybaComponentSpecs = List.of(spec);
    return bundleData;
  }

  @Test
  public void testBundleDataValidationYbaOnlyAcceptsValidRequest() {
    handler.bundleDataValidationYbaOnly(ybaOnlyBundleData("create_application_logs_bundle"));
  }

  @Test
  public void testBundleDataValidationYbaOnlyRejectsScriptOutsideDevopsHome() {
    SupportBundleFormDataV2 bundleData = ybaOnlyBundleData("create_application_logs_bundle");
    bundleData.ybaComponentSpecs.get(0).setScriptPath("/tmp/evil.sh");

    PlatformServiceException exception =
        assertThrows(
            PlatformServiceException.class, () -> handler.bundleDataValidationYbaOnly(bundleData));
    assertEquals(BAD_REQUEST, exception.getHttpStatus());
  }
}
