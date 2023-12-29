package com.yugabyte.troubleshoot.ts.service;

import com.yugabyte.troubleshoot.ts.models.PgStatStatements;
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
}
