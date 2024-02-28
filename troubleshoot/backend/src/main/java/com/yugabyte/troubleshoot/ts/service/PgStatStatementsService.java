package com.yugabyte.troubleshoot.ts.service;

import com.yugabyte.troubleshoot.ts.models.PgStatStatements;
import com.yugabyte.troubleshoot.ts.models.query.QPgStatStatements;
import com.yugabyte.troubleshoot.ts.service.filter.PsStatStatetementsFilter;
import io.ebean.Database;
import io.ebean.annotation.Transactional;
import java.util.*;
import lombok.extern.slf4j.Slf4j;
import org.springframework.stereotype.Service;

@Slf4j
@Service
public class PgStatStatementsService {

  protected final Database database;

  public PgStatStatementsService(Database database) {
    this.database = database;
  }

  @Transactional
  public void save(List<PgStatStatements> stats) {
    database.saveAll(stats);
  }

  public List<PgStatStatements> listAll() {
    return new QPgStatStatements().findList();
  }

  public List<PgStatStatements> list(PsStatStatetementsFilter filter) {
    QPgStatStatements query = new QPgStatStatements();
    if (filter.getUniverseUuid() != null) {
      query.universeId.eq(filter.getUniverseUuid());
    }
    if (filter.getNodeNames() != null) {
      query.nodeName.in(filter.getNodeNames());
    }
    if (filter.getQueryIds() != null) {
      query.queryId.in(filter.getQueryIds());
    }
    if (filter.getStartTimestamp() != null) {
      query.scheduledTimestamp.after(filter.getStartTimestamp());
    }
    if (filter.getEndTimestamp() != null) {
      query.scheduledTimestamp.before(filter.getEndTimestamp());
    }
    return query.findList();
  }
}
