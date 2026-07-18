package com.yugabyte.troubleshoot.ts.service;

import com.google.common.annotations.VisibleForTesting;
import com.yugabyte.troubleshoot.ts.models.ActiveSessionHistory;
import com.yugabyte.troubleshoot.ts.models.query.QActiveSessionHistory;
import com.yugabyte.troubleshoot.ts.service.filter.ActiveSessionHistoryFilter;
import io.ebean.Database;
import io.ebean.annotation.Transactional;
import java.util.List;
import lombok.extern.slf4j.Slf4j;
import org.springframework.stereotype.Service;

@Slf4j
@Service
public class ActiveSessionHistoryService {

  protected final Database database;

  public ActiveSessionHistoryService(Database database) {
    this.database = database;
  }

  @Transactional
  public void save(List<ActiveSessionHistory> stats) {
    database.saveAll(stats);
  }

  @VisibleForTesting
  public List<ActiveSessionHistory> listAll() {
    return new QActiveSessionHistory().findList();
  }

  public List<ActiveSessionHistory> list(ActiveSessionHistoryFilter filter) {
    QActiveSessionHistory query = new QActiveSessionHistory();
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
      query.sampleTime.after(filter.getStartTimestamp());
    }
    if (filter.getEndTimestamp() != null) {
      query.sampleTime.before(filter.getEndTimestamp());
    }
    return query.findList();
  }
}
