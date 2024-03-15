package com.yugabyte.troubleshoot.ts.controllers;

import com.yugabyte.troubleshoot.ts.models.UniverseMetadata;
import com.yugabyte.troubleshoot.ts.service.UniverseMetadataService;
import java.util.List;
import java.util.UUID;
import org.springframework.web.bind.annotation.*;

@RestController
public class UniverseMetadataController {

  private final UniverseMetadataService universeMetadataService;

  public UniverseMetadataController(UniverseMetadataService universeMetadataService) {
    this.universeMetadataService = universeMetadataService;
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
    return universeMetadataService.save(metadata);
  }

  @DeleteMapping("/universe_metadata/{id}")
  public void delete(@PathVariable UUID id) {
    universeMetadataService.delete(id);
  }
}
