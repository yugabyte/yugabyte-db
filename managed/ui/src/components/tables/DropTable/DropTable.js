// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { YBModal } from '../../common/forms/fields';
import { isValidObject } from '../../../utils/ObjectUtils';
import { browserHistory } from 'react-router';

export default class DropTable extends Component {

  constructor(props) {
    super(props);
    this.confirmDropTable = this.confirmDropTable.bind(this);
  }

  confirmDropTable() {
    const { currentTableDetail: { universeUUID, tableUUID }, dropTable, onHide } = this.props;
    dropTable(universeUUID, tableUUID);
    onHide();
    browserHistory.push('/universes/' + universeUUID + "?tab=tables");
  }

  render() {
    const { visible, onHide, currentTableDetail } = this.props;
    if (isValidObject(currentTableDetail.tableDetails)) {
      const tableName = currentTableDetail.tableDetails.tableName;
      const keyspace = currentTableDetail.tableDetails.keyspace;
      let redisMessage = <span />;
      if (currentTableDetail.tableType === "REDIS_TABLE_TYPE") {
        redisMessage = (
          <span>
            <br />
            <br />
            Deleting this table could render your universe unusable for redis workloads.
          </span>
        );
      }
      return (
        <div className="universe-apps-modal">
          <YBModal title={"Delete " + keyspace + "." + tableName}
                   visible={visible}
                   onHide={onHide}
                   showCancelButton={true}
                   cancelLabel={"Cancel"}
                   onFormSubmit={this.confirmDropTable}>
            Are you sure you want to delete this table?
            { redisMessage }
          </YBModal>
        </div>
      );
    }
    return <span />;
  }
}
