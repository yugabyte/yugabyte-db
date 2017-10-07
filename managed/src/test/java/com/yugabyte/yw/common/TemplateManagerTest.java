// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import com.yugabyte.yw.models.AccessKey;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;
import org.apache.commons.io.FileUtils;
import org.junit.After;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.ExpectedException;
import org.junit.runner.RunWith;
import org.mockito.InjectMocks;
import org.mockito.Mock;
import org.mockito.runners.MockitoJUnitRunner;

import java.io.File;
import java.io.IOException;
import java.util.HashMap;
import java.util.LinkedList;
import java.util.List;

import static com.yugabyte.yw.commissioner.Common.CloudType.onprem;
import static com.yugabyte.yw.common.DevopsBase.YBCLOUD_SCRIPT;
import static com.yugabyte.yw.common.ShellProcessHandler.ShellResponse;
import static com.yugabyte.yw.common.TemplateManager.PROVISION_SCRIPT;
import static org.hamcrest.core.Is.is;
import static org.junit.Assert.assertThat;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

@RunWith(MockitoJUnitRunner.class)
public class TemplateManagerTest extends FakeDBApplication {

  private static final String YB_STORAGE_PATH_KEY = "yb.storage.path";
  private static final String YB_STORAGE_PATH_VALUE = "/tmp/yugaware_tests";
  private static final String YB_THIRDPARTY_KEY = "yb.thirdparty.packagePath";
  private static final String YB_THIRDPARTY_VALUE = "/tmp/thirdparty";
  private static final String KEY_CODE = "test-key";

  private Customer testCustomer;
  private Provider testProvider;

  @Mock
  ShellProcessHandler shellProcessHandler;

  @Mock
  play.Configuration mockAppConfig;

  @InjectMocks
  TemplateManager templateManager;

  private AccessKey setupTestAccessKey() {
    AccessKey.KeyInfo keyInfo = new AccessKey.KeyInfo();
    keyInfo.privateKey = "/path/to/pk.pem";
    keyInfo.sshUser = "centos";
    keyInfo.vaultFile = "/path/to/vault";
    keyInfo.vaultPasswordFile = "/path/to/vaultpassword";
    return AccessKey.create(testProvider.uuid, KEY_CODE, keyInfo);
  }

  private List<String> getExpectedCommmand(AccessKey.KeyInfo keyInfo) {
    List<String> cmd = new LinkedList<>();
    cmd.add(YBCLOUD_SCRIPT);
    cmd.add(onprem.name());
    cmd.add(templateManager.getCommandType().toLowerCase());
    cmd.add("template");
    cmd.add("--name");
    cmd.add(PROVISION_SCRIPT);
    cmd.add("--destination");
    cmd.add(YB_STORAGE_PATH_VALUE + "/provision/" + testProvider.uuid);
    cmd.add("--ssh_user");
    cmd.add(keyInfo.sshUser);
    cmd.add("--vars_file");
    cmd.add(keyInfo.vaultFile);
    cmd.add("--vault_password_file");
    cmd.add(keyInfo.vaultPasswordFile);
    cmd.add("--local_package_path");
    cmd.add(YB_THIRDPARTY_VALUE);
    return cmd;
  }

  @Rule
  public ExpectedException expectedException = ExpectedException.none();


  @Before
  public void setUp() {
    testCustomer = ModelFactory.testCustomer();
    testProvider = ModelFactory.awsProvider(testCustomer);
    when(mockAppConfig.getString(YB_STORAGE_PATH_KEY)).thenReturn(YB_STORAGE_PATH_VALUE);
    when(mockAppConfig.getString(YB_THIRDPARTY_KEY)).thenReturn(YB_THIRDPARTY_VALUE);
  }

  @After
  public void tearDown() throws IOException {
    FileUtils.deleteDirectory(new File(YB_STORAGE_PATH_VALUE));
  }

  @Test
  public void testGetOrCreateProvisionFilePathSuccess() {
    String path = templateManager.getOrCreateProvisionFilePath(testProvider.uuid);
    assertThat(path, is(YB_STORAGE_PATH_VALUE + "/provision/" + testProvider.uuid));
  }

  @Test
  public void testTemplateCommandSuccess() {
    AccessKey accessKey = setupTestAccessKey();
    List<String> expectedCommand = getExpectedCommmand(accessKey.getKeyInfo());
    when(shellProcessHandler.run(expectedCommand, new HashMap<>())).thenReturn(ShellResponse.create(0, "{}"));
    templateManager.createProvisionTemplate(accessKey);
    verify(shellProcessHandler, times(1)).run(expectedCommand, new HashMap<>());
  }

  @Test
  public void testTemplateCommandError() {
    AccessKey accessKey = setupTestAccessKey();
    List<String> expectedCommand = getExpectedCommmand(accessKey.getKeyInfo());
    when(shellProcessHandler.run(expectedCommand, new HashMap<>())).thenReturn(ShellResponse.create(1, "foobar"));
    expectedException.expect(RuntimeException.class);
    expectedException.expectMessage("YBCloud command instance (template) failed to execute.");
    templateManager.createProvisionTemplate(accessKey);
  }
}
