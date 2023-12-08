// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import static com.yugabyte.yw.commissioner.Common.CloudType.onprem;
import static com.yugabyte.yw.common.DevopsBase.YBCLOUD_SCRIPT;
import static com.yugabyte.yw.common.TemplateManager.PROVISION_SCRIPT;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.*;

import com.typesafe.config.Config;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.models.AccessKey;
import com.yugabyte.yw.models.AccessKey.KeyInfo;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;
import java.io.File;
import java.io.IOException;
import java.time.Duration;
import java.util.Arrays;
import java.util.LinkedList;
import java.util.List;
import org.apache.commons.io.FileUtils;
import org.junit.After;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.ExpectedException;
import org.junit.runner.RunWith;
import org.mockito.InjectMocks;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class TemplateManagerTest extends FakeDBApplication {
  private static final String YB_STORAGE_PATH_KEY = "yb.storage.path";
  private static final String YB_STORAGE_PATH_VALUE = "/tmp/yugaware_tests/tmt_certs";
  private static final String YB_THIRDPARTY_KEY = "yb.thirdparty.packagePath";
  private static final String YB_THIRDPARTY_VALUE = "/tmp/thirdparty";
  private static final String KEY_CODE = "test-key";

  private Customer testCustomer;
  private Provider testProvider;

  @Mock ShellProcessHandler shellProcessHandler;

  @Mock Config mockConfig;

  @Mock RuntimeConfGetter mockConfGetter;

  @Mock NodeAgentClient nodeAgentClient;

  @InjectMocks TemplateManager templateManager;

  private AccessKey setupTestAccessKey() {
    testProvider.getDetails().sshUser = "centos";
    testProvider.getDetails().sshPort = 3333;
    testProvider.save();
    AccessKey.KeyInfo keyInfo = new AccessKey.KeyInfo();
    keyInfo.privateKey = "/path/to/pk.pem";
    keyInfo.vaultFile = "/path/to/vault";
    keyInfo.vaultPasswordFile = "/path/to/vaultpassword";
    keyInfo.privateKey = "/path/to/pemfile";
    return AccessKey.create(testProvider.getUuid(), KEY_CODE, keyInfo);
  }

  private List<String> getExpectedCommmand(KeyInfo keyInfo) {
    List<String> cmd = new LinkedList<>();
    cmd.add(YBCLOUD_SCRIPT);
    cmd.add(onprem.name());
    cmd.add(templateManager.getCommandType().toLowerCase());
    cmd.add("template");
    cmd.add("--name");
    cmd.add(PROVISION_SCRIPT);
    cmd.add("--destination");
    cmd.add(YB_STORAGE_PATH_VALUE + "/provision/" + testProvider.getUuid());
    cmd.add("--ssh_user");
    cmd.add(testProvider.getDetails().sshUser);
    cmd.add("--vars_file");
    cmd.add(keyInfo.vaultFile);
    cmd.add("--vault_password_file");
    cmd.add(keyInfo.vaultPasswordFile);
    cmd.add("--private_key_file");
    cmd.add(keyInfo.privateKey);
    cmd.add("--local_package_path");
    cmd.add(YB_THIRDPARTY_VALUE);
    cmd.add("--custom_ssh_port");
    cmd.add(testProvider.getDetails().sshPort.toString());
    cmd.add("--provider_id");
    cmd.add(testProvider.getUuid().toString());
    return cmd;
  }

  @Rule public ExpectedException expectedException = ExpectedException.none();

  @Before
  public void setUp() {
    testCustomer = ModelFactory.testCustomer();
    testProvider = ModelFactory.onpremProvider(testCustomer);
    when(mockConfig.getString(YB_STORAGE_PATH_KEY)).thenReturn(YB_STORAGE_PATH_VALUE);
    when(mockConfig.getString(YB_THIRDPARTY_KEY)).thenReturn(YB_THIRDPARTY_VALUE);
    when(mockConfGetter.getGlobalConf(eq(GlobalConfKeys.ssh2Enabled))).thenReturn(false);
    when(mockConfGetter.getGlobalConf(eq(GlobalConfKeys.devopsCommandTimeout)))
        .thenReturn(Duration.ofHours(1));
  }

  @After
  public void tearDown() throws IOException {
    FileUtils.deleteDirectory(new File(YB_STORAGE_PATH_VALUE));
  }

  private void assertAccessKeyInfo(
      AccessKey accessKey,
      boolean airGapInstall,
      boolean passwordlessSudo,
      boolean installNodeExporter,
      boolean setUpChrony) {
    testProvider.refresh();
    assertEquals(airGapInstall, testProvider.getDetails().airGapInstall);
    assertEquals(passwordlessSudo, testProvider.getDetails().passwordlessSudoAccess);
    assertEquals(installNodeExporter, testProvider.getDetails().installNodeExporter);
    assertEquals(setUpChrony, testProvider.getDetails().setUpChrony);
    if (airGapInstall || passwordlessSudo) {
      String expectedProvisionScript =
          String.format(
              "%s/provision/%s/%s",
              YB_STORAGE_PATH_VALUE, accessKey.getProviderUUID(), PROVISION_SCRIPT);
      assertEquals(expectedProvisionScript, testProvider.getDetails().provisionInstanceScript);
    } else {
      assertNull(testProvider.getDetails().provisionInstanceScript);
    }
  }

  @Test
  public void testTemplateCommandWithAirGapEnabled() {
    AccessKey accessKey = setupTestAccessKey();
    List<String> expectedCommand = getExpectedCommmand(accessKey.getKeyInfo());
    expectedCommand.add("--air_gap");
    expectedCommand.add("--install_node_exporter");
    expectedCommand.add("--node_exporter_port");
    expectedCommand.add("9300");
    expectedCommand.add("--node_exporter_user");
    expectedCommand.add("prometheus");
    when(shellProcessHandler.run(eq(expectedCommand), any(ShellProcessContext.class)))
        .thenReturn(ShellResponse.create(0, "{}"));
    templateManager.createProvisionTemplate(
        accessKey, true, false, true, 9300, "prometheus", false, null);
    verify(shellProcessHandler, times(1)).run(eq(expectedCommand), any(ShellProcessContext.class));
    assertAccessKeyInfo(accessKey, true, false, true, false);
  }

  @Test
  public void testTemplateCommandWithAirGapAndPasswordlessSudoAccessEnabled() {
    AccessKey accessKey = setupTestAccessKey();
    List<String> expectedCommand = getExpectedCommmand(accessKey.getKeyInfo());
    expectedCommand.add("--air_gap");
    expectedCommand.add("--passwordless_sudo");
    expectedCommand.add("--install_node_exporter");
    expectedCommand.add("--node_exporter_port");
    expectedCommand.add("9300");
    expectedCommand.add("--node_exporter_user");
    expectedCommand.add("prometheus");
    when(shellProcessHandler.run(eq(expectedCommand), any(ShellProcessContext.class)))
        .thenReturn(ShellResponse.create(0, "{}"));
    templateManager.createProvisionTemplate(
        accessKey, true, true, true, 9300, "prometheus", false, null);
    verify(shellProcessHandler, times(1)).run(eq(expectedCommand), any(ShellProcessContext.class));
    assertAccessKeyInfo(accessKey, true, true, true, false);
  }

  @Test
  public void testTemplateCommandWithPasswordlessSudoAccessEnabled() {
    AccessKey accessKey = setupTestAccessKey();
    List<String> expectedCommand = getExpectedCommmand(accessKey.getKeyInfo());
    expectedCommand.add("--passwordless_sudo");
    expectedCommand.add("--install_node_exporter");
    expectedCommand.add("--node_exporter_port");
    expectedCommand.add("9300");
    expectedCommand.add("--node_exporter_user");
    expectedCommand.add("prometheus");
    when(shellProcessHandler.run(eq(expectedCommand), any(ShellProcessContext.class)))
        .thenReturn(ShellResponse.create(0, "{}"));
    templateManager.createProvisionTemplate(
        accessKey, false, true, true, 9300, "prometheus", false, null);
    verify(shellProcessHandler, times(1)).run(eq(expectedCommand), any(ShellProcessContext.class));
    assertAccessKeyInfo(accessKey, false, true, true, false);
  }

  @Test
  public void testTemplateCommandWithInstallNodeExporterDisabled() {
    AccessKey accessKey = setupTestAccessKey();
    List<String> expectedCommand = getExpectedCommmand(accessKey.getKeyInfo());
    expectedCommand.add("--passwordless_sudo");
    when(shellProcessHandler.run(eq(expectedCommand), any(ShellProcessContext.class)))
        .thenReturn(ShellResponse.create(0, "{}"));
    templateManager.createProvisionTemplate(
        accessKey, false, true, false, 9300, "prometheus", false, null);
    verify(shellProcessHandler, times(1)).run(eq(expectedCommand), any(ShellProcessContext.class));
    assertAccessKeyInfo(accessKey, false, true, false, false);
  }

  @Test
  public void testTemplateCommandWithNTPServers() {
    AccessKey accessKey = setupTestAccessKey();
    List<String> expectedCommand = getExpectedCommmand(accessKey.getKeyInfo());
    List<String> servers = Arrays.asList("0.yb.pool.ntp.org", "1.yb.pool.ntp.org");
    expectedCommand.add("--passwordless_sudo");
    expectedCommand.add("--use_chrony");
    for (String server : servers) {
      expectedCommand.add("--ntp_server");
      expectedCommand.add(server);
    }
    when(shellProcessHandler.run(eq(expectedCommand), any(ShellProcessContext.class)))
        .thenReturn(ShellResponse.create(0, "{}"));
    templateManager.createProvisionTemplate(
        accessKey, false, true, false, 9300, "prometheus", true, servers);
    verify(shellProcessHandler, times(1)).run(eq(expectedCommand), any(ShellProcessContext.class));
    assertAccessKeyInfo(accessKey, false, true, false, true);
  }

  @Test
  public void testTemplateCommandError() {
    AccessKey accessKey = setupTestAccessKey();
    List<String> expectedCommand = getExpectedCommmand(accessKey.getKeyInfo());
    expectedCommand.add("--air_gap");
    expectedCommand.add("--passwordless_sudo");
    expectedCommand.add("--install_node_exporter");
    expectedCommand.add("--node_exporter_port");
    expectedCommand.add("9300");
    expectedCommand.add("--node_exporter_user");
    expectedCommand.add("prometheus");
    when(shellProcessHandler.run(eq(expectedCommand), any(ShellProcessContext.class)))
        .thenReturn(ShellResponse.create(1, "foobar"));
    expectedException.expect(PlatformServiceException.class);
    expectedException.expectMessage("YBCloud command instance (template) failed to execute.");
    templateManager.createProvisionTemplate(
        accessKey, true, true, true, 9300, "prometheus", false, null);
  }
}
