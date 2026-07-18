package com.yugabyte.troubleshoot.ts.controllers;

import com.yugabyte.troubleshoot.ts.models.UniverseDetails;
import com.yugabyte.troubleshoot.ts.models.UniverseMetadata;
import com.yugabyte.troubleshoot.ts.service.BeanValidator;
import com.yugabyte.troubleshoot.ts.service.UniverseDetailsService;
import com.yugabyte.troubleshoot.ts.service.UniverseMetadataService;
import com.yugabyte.troubleshoot.ts.task.UniverseDetailsQuery;
import com.yugabyte.troubleshoot.ts.yba.client.YBAClient;
import java.util.List;
import java.util.UUID;
import org.springframework.web.bind.annotation.*;

@RestController
public class UniverseMetadataController {

  private final UniverseMetadataService universeMetadataService;
  private final UniverseDetailsService universeDetailsService;
  private final BeanValidator beanValidator;

  private final YBAClient ybaClient;

  public UniverseMetadataController(
      UniverseMetadataService universeMetadataService,
      UniverseDetailsService universeDetailsService,
      BeanValidator beanValidator,
      YBAClient ybaClient) {
    this.universeMetadataService = universeMetadataService;
    this.universeDetailsService = universeDetailsService;
    this.beanValidator = beanValidator;
    this.ybaClient = ybaClient;
  }

  @GetMapping("/universe_metadata")
  public List<UniverseMetadata> list() {
    return universeMetadataService.listAll();
  }

  @GetMapping("/universe_metadata/{id}")
  public UniverseMetadata get(@PathVariable UUID id) {
    return universeMetadataService.getOrThrow(id);
  }

  @PutMapping("/universe_metadata/{id}")
  public UniverseMetadata put(@PathVariable UUID id, @RequestBody UniverseMetadata metadata) {
    metadata.setId(id);
    try {
      UniverseDetails details = ybaClient.getUniverseDetails(metadata);
      UniverseDetailsQuery.updateSyncStatusOnSuccess(details);
      universeDetailsService.save(details);
    } catch (Exception e) {
      beanValidator
          .error()
          .global("Failed to fetch universe details: " + e.getMessage())
          .throwError();
    }
    return universeMetadataService.save(metadata);
  }

  @DeleteMapping("/universe_metadata/{id}")
  public void delete(@PathVariable UUID id) {
    universeMetadataService.delete(id);
  }
}
