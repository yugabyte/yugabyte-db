// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.models.Provider;
import org.junit.AfterClass;
import org.junit.Before;
import org.junit.BeforeClass;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.ArgumentCaptor;
import org.mockito.InjectMocks;
import org.mockito.Mock;
import org.mockito.Mockito;
import org.mockito.runners.MockitoJUnitRunner;

import java.io.File;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

import static org.hamcrest.CoreMatchers.notNullValue;
import static org.hamcrest.CoreMatchers.allOf;
import static org.hamcrest.CoreMatchers.equalTo;
import static org.junit.Assert.assertThat;
import static org.junit.Assert.assertTrue;
import static org.mockito.Matchers.anyList;
import static org.mockito.Matchers.anyMap;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.when;


@RunWith(MockitoJUnitRunner.class)
public class AccessManagerTest extends FakeDBApplication {

    @InjectMocks
    AccessManager accessManager;

    @Mock
    ShellProcessHandler shellProcessHandler;

    @Mock
    play.Configuration appConfig;

    private Provider defaultProvider;

    static String TMP_KEYS_PATH = "/tmp/yugaware_tests";

    @BeforeClass
    public static void setUp() {
        new File(TMP_KEYS_PATH).mkdir();
    }

    @AfterClass
    public static void tearDown() {
        File file = new File(TMP_KEYS_PATH);
        file.delete();
    }

    @Before
    public void beforeTest() {
        defaultProvider = ModelFactory.awsProvider(ModelFactory.testCustomer());
        when(appConfig.getString("yb.keys.basePath")).thenReturn(TMP_KEYS_PATH);
        ShellProcessHandler.ShellResponse response = new ShellProcessHandler.ShellResponse();
        response.message = "{\"foo\": \"bar\"}";
        response.code = 0;
        when(shellProcessHandler.run(anyList(), anyMap())).thenReturn(response);
    }


    @Test
    public void testManageAddKeyCommand() {
        ArgumentCaptor<ArrayList> command = ArgumentCaptor.forClass(ArrayList.class);
        ArgumentCaptor<HashMap> cloudCredentials = ArgumentCaptor.forClass(HashMap.class);

        JsonNode result = accessManager.addKey(defaultProvider.uuid, "foo");
        Mockito.verify(shellProcessHandler, times(1)).run((List<String>) command.capture(),
                (Map<String, String>) cloudCredentials.capture());

        String commandStr = String.join(" ", command.getValue());
        String expectedCmd = "bin/ybcloud.sh aws access add-key --key_pair_name foo " +
                "--key_file_path /tmp/yugaware_tests/" + defaultProvider.uuid;
        assertThat(commandStr, allOf(notNullValue(), equalTo(expectedCmd)));
        assertTrue(cloudCredentials.getValue().isEmpty());
        assertThat(result.toString(), allOf(notNullValue(), equalTo("{\"foo\":\"bar\"}")));
    }

    @Test
    public void testManageListKeysCommand() {
        ArgumentCaptor<ArrayList> command = ArgumentCaptor.forClass(ArrayList.class);
        ArgumentCaptor<HashMap> cloudCredentials = ArgumentCaptor.forClass(HashMap.class);

        JsonNode result = accessManager.listKeys(defaultProvider.uuid);
        Mockito.verify(shellProcessHandler, times(1)).run((List<String>) command.capture(),
                (Map<String, String>) cloudCredentials.capture());

        String commandStr = String.join(" ", command.getValue());
        String expectedCmd = "bin/ybcloud.sh aws access list-keys";
        assertThat(commandStr, allOf(notNullValue(), equalTo(expectedCmd)));
        assertTrue(cloudCredentials.getValue().isEmpty());
        assertThat(result.toString(), allOf(notNullValue(), equalTo("{\"foo\":\"bar\"}")));
    }

    @Test
    public void testManageListRegionsCommand() {
        ArgumentCaptor<ArrayList> command = ArgumentCaptor.forClass(ArrayList.class);
        ArgumentCaptor<HashMap> cloudCredentials = ArgumentCaptor.forClass(HashMap.class);

        JsonNode result = accessManager.listRegions(defaultProvider.uuid);
        Mockito.verify(shellProcessHandler, times(1)).run((List<String>) command.capture(),
                (Map<String, String>) cloudCredentials.capture());

        String commandStr = String.join(" ", command.getValue());
        String expectedCmd = "bin/ybcloud.sh aws access list-regions";
        assertThat(commandStr, allOf(notNullValue(), equalTo(expectedCmd)));
        assertTrue(cloudCredentials.getValue().isEmpty());
        assertThat(result.toString(), allOf(notNullValue(), equalTo("{\"foo\":\"bar\"}")));
    }

    @Test
    public void testManageCommandInvalidResponse() {
        ShellProcessHandler.ShellResponse response = new ShellProcessHandler.ShellResponse();
        response.message = "{\"error\": \"Unknown\"}";
        response.code = -1;
        when(shellProcessHandler.run(anyList(), anyMap())).thenReturn(response);
        JsonNode result = accessManager.addKey(defaultProvider.uuid, "foo");
        Mockito.verify(shellProcessHandler, times(1)).run(anyList(), anyMap());
        assertThat(result.get("error").asText(), allOf(notNullValue(),
                equalTo("AccessManager failed to execute")));
    }

    @Test
    public void testInvalidKeysBasePath() {
        when(appConfig.getString("yb.keys.basePath")).thenReturn(null);
        JsonNode result = accessManager.addKey(defaultProvider.uuid, "foo");

        Mockito.verify(shellProcessHandler, times(0)).run(anyList(), anyMap());

        assertThat(result.get("error").asText(), allOf(notNullValue(),
                equalTo("yb.keys.basePath is null or doesn't exist.")));
    }
}
