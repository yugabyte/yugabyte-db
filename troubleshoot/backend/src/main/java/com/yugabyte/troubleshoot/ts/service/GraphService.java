package com.yugabyte.troubleshoot.ts.service;

import com.yugabyte.troubleshoot.ts.models.GraphQuery;
import com.yugabyte.troubleshoot.ts.models.GraphResponse;
import io.ebean.Database;
import java.util.List;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;
import org.springframework.stereotype.Service;

@Slf4j
@Service
public class GraphService {

  protected final Database database;

  public GraphService(Database database) {
    this.database = database;
  }

  public List<GraphResponse> getGraphs(UUID universeUuid, List<GraphQuery> queries) {
    throw new UnsupportedOperationException();
  }
}
