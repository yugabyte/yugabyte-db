package com.yugabyte.yw.models;

import io.ebean.Model;
import javax.persistence.Column;
import javax.persistence.Entity;
import javax.persistence.Table;

@Table
@Entity
public class XClusterTableConfig extends Model {

  @Column(name = "table_id")
  public String tableID;

  public XClusterTableConfig(String tableID) {
    this.tableID = tableID;
  }
}
