// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.models;

import com.yugabyte.yw.common.ExtraMigrationManager;
import io.ebean.Finder;
import io.ebean.Model;
import java.util.List;
import javax.persistence.Column;
import javax.persistence.Entity;
import javax.persistence.Id;
import lombok.Getter;
import lombok.Setter;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

@Entity
@Getter
@Setter
public class ExtraMigration extends Model {
  ExtraMigrationManager extraMigrationManager;

  public static final Logger LOG = LoggerFactory.getLogger(ExtraMigration.class);

  @Id
  @Column(nullable = false, unique = true)
  private String migration;

  public static final Finder<String, ExtraMigration> find =
      new Finder<String, ExtraMigration>(ExtraMigration.class) {};

  public static List<ExtraMigration> getAll() {
    return find.query().orderBy("migration asc").findList();
  }

  public void run(ExtraMigrationManager extraMigrationManager) throws ReflectiveOperationException {
    this.extraMigrationManager = extraMigrationManager;
    LOG.info("Running migration '{}'.", this.getMigration());
    ExtraMigrationManager.class.getMethod(this.getMigration()).invoke(this.extraMigrationManager);
    LOG.info("Completed migration '{}'.", this.getMigration());
    this.delete();
  }
}
