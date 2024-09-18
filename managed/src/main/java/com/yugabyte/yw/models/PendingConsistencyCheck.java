package com.yugabyte.yw.models;

import io.ebean.Finder;
import io.ebean.Model;
import jakarta.persistence.Column;
import jakarta.persistence.Entity;
import jakarta.persistence.Id;
import jakarta.persistence.JoinColumn;
import jakarta.persistence.ManyToOne;
import java.util.List;
import java.util.UUID;
import lombok.Getter;
import lombok.Setter;
import lombok.extern.slf4j.Slf4j;

@Getter
@Setter
@Slf4j
@Entity
public class PendingConsistencyCheck extends Model {
  @Id
  @Column(name = "task_uuid", nullable = false)
  private UUID taskUuid;

  @Column(name = "pending", nullable = false)
  private boolean pending;

  @ManyToOne
  @JoinColumn(name = "universe_uuid", referencedColumnName = "universe_uuid")
  private Universe universe;

  /** Query Helper for PendingConsistencyCheck with taskUuid */
  public static final Finder<UUID, PendingConsistencyCheck> find =
      new Finder<UUID, PendingConsistencyCheck>(PendingConsistencyCheck.class) {};

  public static PendingConsistencyCheck create(UUID taskUuid, Universe universe) {
    PendingConsistencyCheck pend = new PendingConsistencyCheck();
    pend.setTaskUuid(taskUuid);
    pend.setUniverse(universe);
    pend.setPending(true);
    pend.save();
    return pend;
  }

  public static List<PendingConsistencyCheck> getAll() {
    return find.query().where().findList();
  }
}
