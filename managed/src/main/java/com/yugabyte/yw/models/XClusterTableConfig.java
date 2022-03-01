package com.yugabyte.yw.models;

import io.ebean.Model;
import javax.persistence.Column;
import javax.persistence.Entity;
import javax.persistence.Table;
import lombok.AllArgsConstructor;
import lombok.Data;
import lombok.EqualsAndHashCode;

@Table
@Entity
@Data
@AllArgsConstructor
@EqualsAndHashCode(callSuper = false)
public class XClusterTableConfig extends Model {
  @Column(name = "table_id")
  private String tableID;

  @Column(name = "stream_id")
  private String streamID;

  public XClusterTableConfig(String tableID) {
    this.tableID = tableID;
  }
}
