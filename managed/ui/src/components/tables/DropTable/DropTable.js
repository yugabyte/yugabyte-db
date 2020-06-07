// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { YBModal } from '../../common/forms/fields';
import { browserHistory } from 'react-router';
import PropTypes from 'prop-types';
import { isNonEmptyObject } from '../../../utils/ObjectUtils';

export default class DropTable extends Component {
  static propTypes = {
    tableInfo: PropTypes.object
  }

  confirmDropTable = () => {
    const {
      universeDetails: { universeUUID },
      tableInfo: { tableID },
      dropTable,
      onHide
    } = this.props;
    dropTable(universeUUID, tableID);
    onHide();
    browserHistory.push('/universes/' + universeUUID + "/tables");
  };

  render() {
    if (!isNonEmptyObject(this.props.tableInfo)) {
      return <span />;
    }
    const { visible, onHide, tableInfo: { keySpace, tableName } } = this.props;

    return (
      <div className="universe-apps-modal">
        <YBModal title={"Delete " + keySpace + "." + tableName}
                 visible={visible}
                 onHide={onHide}
                 showCancelButton={true}
                 cancelLabel={"Cancel"}
                 onFormSubmit={this.confirmDropTable}>
          Are you sure you want to delete this table?
        </YBModal>
      </div>
    );
  }
}
