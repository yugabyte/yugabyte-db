package com.yugabyte.troubleshoot.ts.controllers;

import com.google.common.collect.ImmutableList;
import com.yugabyte.troubleshoot.ts.models.*;
import com.yugabyte.troubleshoot.ts.service.RuntimeConfigService;
import java.util.List;
import java.util.UUID;
import org.springframework.web.bind.annotation.*;

@RestController
public class RuntimeConfigController {

  private final RuntimeConfigService runtimeConfigService;

  public RuntimeConfigController(RuntimeConfigService runtimeConfigService) {
    this.runtimeConfigService = runtimeConfigService;
  }

  @GetMapping("/runtime_config_entry/{id}")
  public List<RuntimeConfigEntry> getEntries(@PathVariable("id") UUID scopeUuid) {
    return runtimeConfigService.listByScopeUuids(ImmutableList.of(scopeUuid));
  }

  @GetMapping("/runtime_config_entry/{id}/{path}")
  public RuntimeConfigEntry getEntry(
      @PathVariable("id") UUID scopeUuid, @PathVariable("path") String path) {
    return runtimeConfigService.get(new RuntimeConfigEntryKey(scopeUuid, path));
  }

  @PutMapping("/runtime_config_entry")
  public RuntimeConfigEntry setEntry(@RequestBody RuntimeConfigEntry entry) {
    return runtimeConfigService.save(entry);
  }

  @DeleteMapping("/runtime_config_entry/{id}/{path}")
  public void deleteEntry(@PathVariable("id") UUID scopeUuid, @PathVariable("path") String path) {
    runtimeConfigService.delete(new RuntimeConfigEntryKey(scopeUuid, path));
  }
}
