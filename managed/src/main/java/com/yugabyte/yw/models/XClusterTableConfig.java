package com.yugabyte.yw.models;

import io.ebean.Model;
import javax.persistence.Column;
import javax.persistence.Entity;
import javax.persistence.Table;

@Table
@Entity
public class XClusterTableConfig extends Model {

  @Column(name = "table_id")
  private String tableID;

  public String getTableID() {
    return this.tableID;
  }

  public void setTableID(String tableID) {
    this.tableID = tableID;
  }

  public XClusterTableConfig(String tableID) {
    this.tableID = tableID;
  }
}
