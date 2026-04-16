package com.yugabyte.troubleshoot.ts.controller;

import static com.yugabyte.troubleshoot.ts.TestUtils.formatJson;
import static org.assertj.core.api.Assertions.assertThat;
import static org.springframework.test.web.client.match.MockRestRequestMatchers.requestTo;
import static org.springframework.test.web.client.response.MockRestResponseCreators.withSuccess;
import static org.springframework.test.web.servlet.request.MockMvcRequestBuilders.*;
import static org.springframework.test.web.servlet.result.MockMvcResultHandlers.print;
import static org.springframework.test.web.servlet.result.MockMvcResultMatchers.status;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.google.common.collect.ImmutableList;
import com.yugabyte.troubleshoot.ts.models.UniverseDetails;
import com.yugabyte.troubleshoot.ts.models.UniverseMetadata;
import com.yugabyte.troubleshoot.ts.service.UniverseDetailsServiceTest;
import com.yugabyte.troubleshoot.ts.service.UniverseMetadataService;
import com.yugabyte.troubleshoot.ts.service.UniverseMetadataServiceTest;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.http.MediaType;
import org.springframework.test.web.client.MockRestServiceServer;
import org.springframework.test.web.servlet.MockMvc;
import org.springframework.test.web.servlet.MvcResult;
import org.springframework.web.client.RestTemplate;

@ControllerTest
public class UniverseMetadataControllerTest {

  @Autowired private MockMvc mockMvc;
  @Autowired private ObjectMapper objectMapper;
  @Autowired private UniverseMetadataService universeMetadataService;
  @Autowired private RestTemplate ybaClientTemplate;

  private MockRestServiceServer server;
  private UniverseMetadata metadata;

  @BeforeEach
  public void setUp() {
    metadata = UniverseMetadataServiceTest.testData();
    universeMetadataService.save(metadata);
    server = MockRestServiceServer.createServer(ybaClientTemplate);
  }

  @Test
  public void testGetAll() throws Exception {
    MvcResult result =
        this.mockMvc
            .perform(get("/universe_metadata"))
            .andDo(print())
            .andExpect(status().isOk())
            .andReturn();
    assertThat(formatJson(result.getResponse().getContentAsString()))
        .isEqualTo(objectMapper.writeValueAsString(ImmutableList.of(metadata)));
  }

  @Test
  public void testGet() throws Exception {
    MvcResult result =
        this.mockMvc
            .perform(get("/universe_metadata/" + metadata.getId()))
            .andDo(print())
            .andExpect(status().isOk())
            .andReturn();
    assertThat(formatJson(result.getResponse().getContentAsString()))
        .isEqualTo(objectMapper.writeValueAsString(metadata));
  }

  @Test
  public void testPut() throws Exception {
    metadata.setApiToken("new_token");

    UniverseDetails universeDetails = UniverseDetailsServiceTest.testData(metadata.getId());
    this.server
        .expect(
            requestTo(
                "http://localhost:9000/api/v1/customers/"
                    + metadata.getCustomerId()
                    + "/universes/"
                    + metadata.getId()))
        .andRespond(
            withSuccess(
                objectMapper.writeValueAsString(universeDetails), MediaType.APPLICATION_JSON));

    MvcResult result =
        this.mockMvc
            .perform(
                put("/universe_metadata/" + metadata.getId())
                    .content(objectMapper.writeValueAsString(metadata))
                    .contentType(MediaType.APPLICATION_JSON)
                    .accept(MediaType.APPLICATION_JSON))
            .andDo(print())
            .andExpect(status().isOk())
            .andReturn();
    assertThat(formatJson(result.getResponse().getContentAsString()))
        .isEqualTo(objectMapper.writeValueAsString(metadata));
  }

  @Test
  public void testDelete() throws Exception {
    metadata.setApiToken("new_token");

    this.mockMvc
        .perform(delete("/universe_metadata/" + metadata.getId()))
        .andDo(print())
        .andExpect(status().isOk());
  }
}
