// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import static com.yugabyte.yw.common.ShellProcessHandler.YB_LOGS_MAX_MSG_SIZE;
import static org.hamcrest.CoreMatchers.allOf;
import static org.hamcrest.CoreMatchers.containsString;
import static org.hamcrest.CoreMatchers.equalTo;
import static org.hamcrest.CoreMatchers.notNullValue;
import static org.junit.Assert.assertNotEquals;
import static org.junit.Assert.assertThat;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.when;

import com.typesafe.config.Config;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import java.io.File;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import junit.framework.TestCase;
import org.apache.commons.io.FileUtils;
import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class ShellProcessHandlerTest extends TestCase {
  private ShellProcessHandler shellProcessHandler;

  @Mock RuntimeConfigFactory mockRuntimeConfigFactory;

  @Mock Config mockConfig;

  @Mock PlatformScheduler mockPlatformScheduler;

  @Mock RuntimeConfGetter runtimeConfGetter;

  static String TMP_STORAGE_PATH = "/tmp/yugaware_tests/spht_certs";
  static final String COMMAND_OUTPUT_LOGS_DELETE = "yb.logs.cmdOutputDelete";

  @Before
  public void beforeTest() {
    new File(TMP_STORAGE_PATH).mkdirs();
    when(mockConfig.getString("yb.devops.home")).thenReturn(TMP_STORAGE_PATH);
    when(mockRuntimeConfigFactory.globalRuntimeConf()).thenReturn(mockConfig);
    when(mockConfig.getBoolean(COMMAND_OUTPUT_LOGS_DELETE)).thenReturn(true);
    when(mockConfig.getBytes(YB_LOGS_MAX_MSG_SIZE)).thenReturn(2000L);
    when(runtimeConfGetter.getGlobalConf(eq(GlobalConfKeys.ybTmpDirectoryPath))).thenReturn("/tmp");
    ShellLogsManager shellLogsManager =
        new ShellLogsManager(mockPlatformScheduler, mockRuntimeConfigFactory, runtimeConfGetter);
    shellProcessHandler = new ShellProcessHandler(mockConfig, shellLogsManager);
  }

  @After
  public void tearDown() throws IOException {
    FileUtils.deleteDirectory(new File(TMP_STORAGE_PATH));
  }

  @Test
  public void testRunWithValidCommandAndDevopsHome() {
    List<String> command = new ArrayList<String>();
    command.add("pwd");
    ShellResponse response = shellProcessHandler.run(command, new HashMap<>());
    assertEquals(0, response.code);
    assertThat(response.message, allOf(notNullValue(), containsString(TMP_STORAGE_PATH)));
    assertEquals(response.message.trim(), response.message);
  }

  @Test
  public void testPartialLineOutput() throws IOException {
    List<String> command = new ArrayList<String>();
    String partialLineCmd = "printf foo && sleep 1 && printf bar";
    String fileName = createTestShellScript(partialLineCmd);
    command.add(fileName);
    ShellResponse response = shellProcessHandler.run(command, new HashMap<>());
    assertEquals(0, response.code);
    assertEquals(response.message.trim(), "foobar");
  }

  @Test
  public void testRunWithInvalidDevopsHome() {
    when(mockConfig.getString("yb.devops.home")).thenReturn("/foo");
    List<String> command = new ArrayList<String>();
    command.add("pwd");
    ShellResponse response = shellProcessHandler.run(command, new HashMap<>());
    assertEquals(-1, response.code);
    assertThat(
        response.message,
        allOf(
            notNullValue(),
            equalTo(
                "Cannot run program \"pwd\" (in directory \"/foo\"): "
                    + "error=2, No such file or directory")));
  }

  @Test
  public void testRunWithInvalidCommand() throws IOException {
    String testCmd = ">&2 echo error; sleep 2; echo foobar; sleep 2; echo more; exit 255";
    String fileName = createTestShellScript(testCmd);
    List<String> command = new ArrayList<String>();
    command.add(fileName);
    ShellResponse response = shellProcessHandler.run(command, new HashMap<>());
    assertEquals(255, response.code);
    assertThat(response.message.trim(), allOf(notNullValue(), equalTo("error")));
  }

  private String createTestShellScript(String cmd) throws IOException {
    Path fileName = Files.createTempFile(Paths.get(TMP_STORAGE_PATH), "yw_test", ".sh");
    Files.write(fileName, ("#/bin/bash\n" + cmd).getBytes());
    fileName.toFile().setExecutable(true);
    return fileName.toString();
  }

  @Test
  public void testLongCommand() throws IOException {
    String testCmd = "echo output; >&2 echo error; sleep 20";
    String fileName = createTestShellScript(testCmd);
    List<String> command = new ArrayList<String>();
    command.add(fileName);
    long startMs = System.currentTimeMillis();
    ShellResponse response =
        shellProcessHandler.run(
            command, ShellProcessContext.builder().logCmdOutput(true).timeoutSecs(5).build());
    long durationMs = System.currentTimeMillis() - startMs;
    assert (durationMs < 7000); // allow for some slack on loaded Jenkins servers
    assertNotEquals(0, response.code);
    assertThat(response.message.trim(), allOf(notNullValue(), equalTo("error")));
  }

  @Test
  public void testGetPythonErrMsg() {
    String errMsg =
        "<yb-python-error>{\"type\": \"YBOpsRuntimeError\","
            + "\"message\": \"Runtime error: Instance: i does not exist\","
            + "\"file\": \"/Users/test/code/yugabyte-db/managed/devops/venv/bin/ybcloud.py\","
            + "\"method\": \"<module>\", \"line\": 4}</yb-python-error>";
    String out = ShellProcessHandler.getPythonErrMsg(0, errMsg);
    assertNull(out);
    out = ShellProcessHandler.getPythonErrMsg(2, errMsg);
    assertEquals("YBOpsRuntimeError: Runtime error: Instance: i does not exist", out);
    out = ShellProcessHandler.getPythonErrMsg(2, "{}");
    assertNull(out);
  }
}
