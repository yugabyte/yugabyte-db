package com.yugabyte.troubleshoot.ts.service;

import com.yugabyte.troubleshoot.ts.models.UniverseDetails;
import com.yugabyte.troubleshoot.ts.models.query.QUniverseDetails;
import io.ebean.Database;
import java.util.Collection;
import java.util.List;
import java.util.UUID;
import org.springframework.stereotype.Service;

@Service
public class UniverseDetailsService extends TSMetadataServiceBase<UniverseDetails, UUID> {

  public UniverseDetailsService(Database ebeanServer, BeanValidator beanValidator) {
    super(ebeanServer, beanValidator);
  }

  @Override
  public String entityType() {
    return "Universe Details";
  }

  @Override
  public List<UniverseDetails> listByIds(Collection<UUID> ids) {
    return new QUniverseDetails().universeUUID.in(ids).findList();
  }

  public List<UniverseDetails> listAll() {
    return new QUniverseDetails().findList();
  }
}
