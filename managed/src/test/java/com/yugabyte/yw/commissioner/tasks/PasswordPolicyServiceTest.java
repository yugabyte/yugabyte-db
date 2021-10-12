// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks;

import com.typesafe.config.Config;

import org.junit.Test;
import org.junit.Rule;

import javax.inject.Inject;

import junitparams.Parameters;
import junitparams.JUnitParamsRunner;
import junitparams.Parameters;
import junitparams.converters.Nullable;

import org.junit.Before;
import org.junit.Test.None;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoRule;
import org.mockito.junit.MockitoJUnit;

import org.apache.commons.lang3.StringUtils;
import play.data.validation.ValidationError;

import javax.inject.Inject;
import javax.inject.Singleton;
import java.util.ArrayList;
import java.util.List;
import java.util.Objects;
import java.util.UUID;
import java.util.stream.Collectors;

import static play.mvc.Http.Status.BAD_REQUEST;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.assertThat;
import static org.mockito.Mockito.when;
import static org.mockito.Mockito.mock;
import org.mockito.Mock;

import org.mockito.InjectMocks;
import org.mockito.Spy;
import static org.mockito.Mockito.spy;

import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.password.PasswordPolicyService;
import com.yugabyte.yw.forms.PasswordPolicyFormData;

@RunWith(JUnitParamsRunner.class)
public class PasswordPolicyServiceTest extends CommissionerBaseTest {

  private PasswordPolicyService passwordPolicyService;
  @Mock private Config config;
  // private PasswordPolicyFormData passwordPolicyFormData;

  @Rule public MockitoRule rule = MockitoJUnit.rule();

  @Before
  public void initValues() {
    passwordPolicyService = new PasswordPolicyService(config);
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
