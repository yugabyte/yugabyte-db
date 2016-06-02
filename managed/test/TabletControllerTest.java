/**
 * Copyright (c) YugaByte, Inc.
 *
 * Created by ram on 6/1/16.
 */
import com.fasterxml.jackson.databind.JsonNode;
import controllers.TabletController;
import org.junit.*;
import org.yb.client.ListTabletServersResponse;
import org.yb.client.YBClient;
import play.libs.Json;
import play.mvc.*;

import static org.junit.Assert.*;
import static play.mvc.Http.Status.OK;
import services.YBClientService;
import java.util.Arrays;
import java.util.List;
import static org.mockito.Mockito.*;
import static play.test.Helpers.contentAsString;

public class TabletControllerTest {
    private YBClientService mockService;
    private TabletController tabletController;
    private YBClient mockClient;
    private ListTabletServersResponse mockResponse;


    @Before
    public void setUp() throws Exception {
        mockClient = mock(YBClient.class);
        mockService = mock(YBClientService.class);
        mockResponse = mock(ListTabletServersResponse.class);
        when(mockClient.listTabletServers()).thenReturn(mockResponse);
        when(mockService.getClient()).thenReturn(mockClient);
        tabletController = new TabletController(mockService);
    }

    @Test
    public void testListTabletsSuccess() throws Exception {
        when(mockResponse.getTabletServersCount()).thenReturn(2);
        List<String> mockTabletUUIDs = Arrays.asList("UUID1", "UUID2");
        when(mockResponse.getTabletServersList()).thenReturn(mockTabletUUIDs);
        Result r = tabletController.list();
        JsonNode json = Json.parse(contentAsString(r));
        assertEquals(OK, r.status());
        assertTrue(json.get("tablets").isArray());
   }

    @Test
    public void testListTabletsFailure() throws Exception {
        when(mockResponse.getTabletServersCount()).thenThrow(new RuntimeException("Unknown Error"));
        Result r = tabletController.list();
        assertEquals(500, r.status());
        assertEquals("Error: Unknown Error", contentAsString(r));
    }
}
