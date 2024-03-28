package com.yugabyte.troubleshoot.ts.controller;

import static org.assertj.core.api.Assertions.assertThat;
import static org.springframework.test.web.servlet.request.MockMvcRequestBuilders.*;
import static org.springframework.test.web.servlet.result.MockMvcResultHandlers.print;
import static org.springframework.test.web.servlet.result.MockMvcResultMatchers.status;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.google.common.collect.ImmutableList;
import com.yugabyte.troubleshoot.ts.models.RuntimeConfigEntry;
import com.yugabyte.troubleshoot.ts.models.UniverseMetadata;
import com.yugabyte.troubleshoot.ts.service.RuntimeConfigService;
import com.yugabyte.troubleshoot.ts.service.RuntimeConfigServiceTest;
import com.yugabyte.troubleshoot.ts.service.UniverseMetadataService;
import com.yugabyte.troubleshoot.ts.service.UniverseMetadataServiceTest;
import java.util.List;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.http.MediaType;
import org.springframework.test.web.servlet.MockMvc;
import org.springframework.test.web.servlet.MvcResult;

@ControllerTest
public class RuntimeConfigControllerTest {

  @Autowired private MockMvc mockMvc;
  @Autowired private ObjectMapper objectMapper;
  @Autowired private UniverseMetadataService universeMetadataService;
  @Autowired private RuntimeConfigService runtimeConfigService;
  private UniverseMetadata metadata;
  private List<RuntimeConfigEntry> entries;

  @BeforeEach
  public void setUp() {
    metadata = UniverseMetadataServiceTest.testData();
    universeMetadataService.save(metadata);

    entries = RuntimeConfigServiceTest.testData(metadata);
    runtimeConfigService.save(entries);
  }

  @Test
  public void testGetForScope() throws Exception {
    MvcResult result =
        this.mockMvc
            .perform(get("/runtime_config_entry/" + metadata.getId()))
            .andDo(print())
            .andExpect(status().isOk())
            .andReturn();
    assertThat(result.getResponse().getContentAsString())
        .isEqualTo(
            objectMapper.writeValueAsString(ImmutableList.of(entries.get(0), entries.get(1))));
  }

  @Test
  public void testGet() throws Exception {
    MvcResult result =
        this.mockMvc
            .perform(
                get(
                    "/runtime_config_entry/"
                        + metadata.getId()
                        + "/anomaly.query_latency.batch_size"))
            .andDo(print())
            .andExpect(status().isOk())
            .andReturn();
    assertThat(result.getResponse().getContentAsString())
        .isEqualTo(objectMapper.writeValueAsString(entries.get(0)));
  }

  @Test
  public void testPut() throws Exception {
    RuntimeConfigEntry updated =
        new RuntimeConfigEntry(metadata.getId(), "anomaly.query_latency.batch_size", "30");

    MvcResult result =
        this.mockMvc
            .perform(
                put("/runtime_config_entry")
                    .content(objectMapper.writeValueAsString(updated))
                    .contentType(MediaType.APPLICATION_JSON)
                    .accept(MediaType.APPLICATION_JSON))
            .andDo(print())
            .andExpect(status().isOk())
            .andReturn();
    assertThat(result.getResponse().getContentAsString())
        .isEqualTo(objectMapper.writeValueAsString(updated));
  }

  @Test
  public void testDelete() throws Exception {
    this.mockMvc
        .perform(
            delete(
                "/runtime_config_entry/" + metadata.getId() + "/anomaly.query_latency.batch_size"))
        .andDo(print())
        .andExpect(status().isOk());
  }
}
