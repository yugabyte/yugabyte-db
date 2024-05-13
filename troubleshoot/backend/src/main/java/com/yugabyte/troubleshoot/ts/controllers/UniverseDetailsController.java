package com.yugabyte.troubleshoot.ts.controllers;

import com.yugabyte.troubleshoot.ts.models.UniverseDetails;
import com.yugabyte.troubleshoot.ts.service.UniverseDetailsService;
import java.util.List;
import java.util.UUID;
import org.springframework.web.bind.annotation.*;

@RestController
public class UniverseDetailsController {

  private final UniverseDetailsService universeDetailsService;

  public UniverseDetailsController(UniverseDetailsService universeDetailsService) {
    this.universeDetailsService = universeDetailsService;
  }

  @GetMapping("/universe_details")
  public List<UniverseDetails> list() {
    return universeDetailsService.listAll();
  }

  @GetMapping("/universe_details/{id}")
  public UniverseDetails get(@PathVariable UUID id) {
    return universeDetailsService.getOrThrow(id);
  }
}
