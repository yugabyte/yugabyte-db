package com.yugabyte.troubleshoot.ts.service;

import com.yugabyte.troubleshoot.ts.models.Anomaly;
import io.ebean.Database;
import java.util.List;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;
import org.springframework.stereotype.Service;

@Slf4j
@Service
public class TroubleshootingService {

  protected final Database database;

  public TroubleshootingService(Database database) {
    this.database = database;
  }

  public List<Anomaly> findAnomalies(UUID universeUuid) {
    throw new UnsupportedOperationException();
  }
}
