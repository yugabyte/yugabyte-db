package com.yugabyte.troubleshoot.ts.service;

import com.yugabyte.troubleshoot.ts.models.UniverseMetadata;
import com.yugabyte.troubleshoot.ts.models.query.QUniverseMetadata;
import io.ebean.Database;
import java.util.Collection;
import java.util.List;
import java.util.UUID;
import org.springframework.stereotype.Service;

@Service
public class UniverseMetadataService extends TSMetadataServiceBase<UniverseMetadata, UUID> {

  public UniverseMetadataService(Database ebeanServer, BeanValidator beanValidator) {
    super(ebeanServer, beanValidator);
  }

  @Override
  public String entityType() {
    return "Universe Metadata";
  }

  @Override
  public List<UniverseMetadata> listByIds(Collection<UUID> ids) {
    return new QUniverseMetadata().id.in(ids).findList();
  }

  public List<UniverseMetadata> listAll() {
    return new QUniverseMetadata().findList();
  }
}
