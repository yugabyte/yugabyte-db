package com.yugabyte.troubleshoot.ts.service;

import com.google.common.collect.ImmutableList;
import com.yugabyte.troubleshoot.ts.models.RuntimeConfig;
import com.yugabyte.troubleshoot.ts.models.RuntimeConfigEntry;
import com.yugabyte.troubleshoot.ts.models.RuntimeConfigEntryKey;
import com.yugabyte.troubleshoot.ts.models.UniverseMetadata;
import com.yugabyte.troubleshoot.ts.models.query.QRuntimeConfigEntry;
import io.ebean.Database;
import java.util.*;
import java.util.function.Function;
import java.util.stream.Collectors;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.core.env.Environment;
import org.springframework.stereotype.Service;

@Service
public class RuntimeConfigService
    extends TSMetadataServiceBase<RuntimeConfigEntry, RuntimeConfigEntryKey> {

  public static final UUID GLOBAL_SCOPE = new UUID(0, 0);

  @Autowired Environment environment;

  public RuntimeConfigService(Database ebeanServer, BeanValidator beanValidator) {
    super(ebeanServer, beanValidator);
  }

  @Override
  public String entityType() {
    return "Runtime Config Entry";
  }

  @Override
  public List<RuntimeConfigEntry> listByIds(Collection<RuntimeConfigEntryKey> ids) {
    QRuntimeConfigEntry query = new QRuntimeConfigEntry().or();
    for (RuntimeConfigEntryKey id : ids) {
      query = query.and().key.scopeUUID.eq(id.getScopeUUID()).key.path.eq(id.getPath()).endAnd();
    }
    return query.endOr().findList();
  }

  public List<RuntimeConfigEntry> listByScopeUuids(Collection<UUID> scopeUuids) {
    return new QRuntimeConfigEntry().key.scopeUUID.in(scopeUuids).findList();
  }

  public RuntimeConfig getUniverseConfig(UniverseMetadata metadata) {
    List<UUID> scopeUuids =
        ImmutableList.of(metadata.getId(), metadata.getCustomerId(), GLOBAL_SCOPE);
    Map<String, Map<UUID, RuntimeConfigEntry>> configEntries =
        listByScopeUuids(scopeUuids).stream()
            .collect(
                Collectors.groupingBy(
                    RuntimeConfigEntry::getPath,
                    Collectors.toMap(RuntimeConfigEntry::getScopeUUID, Function.identity())));
    Map<String, String> customConfig =
        configEntries.entrySet().stream()
            .collect(
                Collectors.toMap(
                    Map.Entry::getKey,
                    e ->
                        scopeUuids.stream()
                            .map(uuid -> e.getValue().get(uuid))
                            .filter(Objects::nonNull)
                            .map(RuntimeConfigEntry::getValue)
                            .findFirst()
                            .orElseThrow()));
    return new RuntimeConfig(environment, customConfig);
  }

  @Override
  protected void validate(RuntimeConfigEntry entity, RuntimeConfigEntry before) {
    super.validate(entity, before);
    if (environment.getProperty(entity.getPath()) == null) {
      beanValidator
          .error()
          .forField("path", "Property does not exist for path " + entity.getPath());
    }
  }
}
