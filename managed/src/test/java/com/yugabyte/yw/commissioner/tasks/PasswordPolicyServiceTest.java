// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks;

import static org.mockito.Mockito.when;

import com.typesafe.config.Config;
import com.yugabyte.yw.common.ApiHelper;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.password.PasswordPolicyService;
import junitparams.JUnitParamsRunner;
import junitparams.Parameters;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnit;
import org.mockito.junit.MockitoRule;

@RunWith(JUnitParamsRunner.class)
public class PasswordPolicyServiceTest extends CommissionerBaseTest {

  private PasswordPolicyService passwordPolicyService;
  @Mock private Config config;
  @Mock private ApiHelper apiHelper;

  @Rule public MockitoRule rule = MockitoJUnit.rule();

  @Before
  public void initValues() {
    passwordPolicyService = new PasswordPolicyService(config, apiHelper);
    when(config.getInt("yb.pwdpolicy.default_min_length")).thenReturn(8);
    when(config.getInt("yb.pwdpolicy.default_min_uppercase")).thenReturn(1);
    when(config.getInt("yb.pwdpolicy.default_min_lowercase")).thenReturn(1);
    when(config.getInt("yb.pwdpolicy.default_min_digits")).thenReturn(1);
    when(config.getInt("yb.pwdpolicy.default_min_special_chars")).thenReturn(1);
  }

  @Test(expected = PlatformServiceException.class)
  @Parameters({"@Yugabyte", "#yugaWare"})
  public void testPasswordWithNoNumber(String password) {
    passwordPolicyService.checkPasswordPolicy(null, password);
  }

  @Test(expected = PlatformServiceException.class)
  @Parameters({"@123#YUGA"})
  public void testPasswordWithNoLowerCase(String password) {
    passwordPolicyService.checkPasswordPolicy(null, password);
  }

  @Test(expected = PlatformServiceException.class)
  @Parameters({"@123#yuga"})
  public void testPasswordWithNoUpperCase(String password) {
    passwordPolicyService.checkPasswordPolicy(null, password);
  }

  @Test(expected = PlatformServiceException.class)
  @Parameters({"123yuga"})
  public void testPasswordWithNoSpecialChar(String password) {
    passwordPolicyService.checkPasswordPolicy(null, password);
  }

  @Test(expected = Test.None.class)
  @Parameters({"@123Yuga"})
  public void testPasswordWithAllParameters(String password) {
    passwordPolicyService.checkPasswordPolicy(null, password);
  }
}
