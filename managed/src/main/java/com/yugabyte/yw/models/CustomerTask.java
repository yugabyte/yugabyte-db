// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models;

import com.avaje.ebean.Model;
import com.avaje.ebean.annotation.EnumValue;
import com.fasterxml.jackson.annotation.JsonBackReference;
import com.fasterxml.jackson.annotation.JsonFormat;
import play.data.validation.Constraints;

import javax.persistence.*;
import java.util.Date;
import java.util.UUID;

@Entity
public class CustomerTask extends Model {
  public enum TargetType {
    @EnumValue("Universe")
    Universe,

    @EnumValue("Table")
    Table
  }

  public enum TaskType {
    @EnumValue("Create")
    Create,

    @EnumValue("Update")
    Update,

    @EnumValue("Delete")
    Delete
  }

  @Id @GeneratedValue(strategy= GenerationType.IDENTITY)
  private Long id;

  @Constraints.Required
  @Column(nullable = false)
  private UUID customerUUID;
  public UUID getCustomerUUID() { return customerUUID; }

  @Constraints.Required
  @Column(nullable = false)
  private UUID taskUUID;
  public UUID getTaskUUID() { return taskUUID; }

  @Constraints.Required
  @Column(nullable = false)
  private TargetType targetType;
  public TargetType getTarget() { return targetType; }

  @Constraints.Required
  @Column(nullable = false)
  private String targetName;
  public String getTargetName() { return targetName; }

  @Constraints.Required
  @Column(nullable = false)
  private TaskType type;
  public TaskType getType() { return type; }

  @Constraints.Required
  @Column(nullable = false)
  private UUID universeUUID;
  public UUID getUniverseUUID() { return universeUUID; }

  @Constraints.Required
  @Column(nullable = false)
  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd hh:mm:ss")
  private Date createTime;
  public Date getCreateTime() { return createTime; }

  @Column
  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd hh:mm:ss")
  private Date completionTime;
  public Date getCompletionTime() { return completionTime; }
  public void markAsCompleted() {
    completionTime = new Date();
    save();
  }

  public static final Find<Long, CustomerTask> find = new Find<Long, CustomerTask>(){};

  public static CustomerTask create(Customer customer, Universe universe, UUID taskUUID, TargetType targetType, TaskType type, String targetName) {
    CustomerTask th = new CustomerTask();
    th.customerUUID = customer.uuid;
    th.universeUUID = universe.universeUUID;
    th.taskUUID = taskUUID;
    th.targetType = targetType;
    th.type = type;
    th.targetName = targetName;
    th.createTime = new Date();
    th.save();
    return th;
  }

  public String getFriendlyDescription() {
    StringBuilder sb = new StringBuilder();

    switch(this.type) {
      case Create:
        sb.append( (completionTime != null) ? "Created " : "Creating ");
        break;
      case Update:
        sb.append( (completionTime != null) ? "Updated " : "Updating ");
        break;

      case Delete:
        sb.append( (completionTime != null) ? "Deleted " : "Deleting ");
        break;
    }

    sb.append(targetType.name());
    sb.append(" : " + targetName);
    return sb.toString();
  }
}
