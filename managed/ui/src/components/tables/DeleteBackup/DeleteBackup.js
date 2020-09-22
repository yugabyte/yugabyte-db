// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { YBModal } from '../../common/forms/fields';
import { browserHistory } from 'react-router';
import PropTypes from 'prop-types';
import { isNonEmptyObject } from '../../../utils/ObjectUtils';

export default class DeleteBackup extends Component {
  static propTypes = {
    tableInfo: PropTypes.object
  }

  confirmDeleteBackup = () => {
    const {
      tableInfo: { backupUUID },
      deleteBackup,
      onHide
    } = this.props;
    deleteBackup(backupUUID);
    onHide();
  };

  render() {
    if (!isNonEmptyObject(this.props.tableInfo)) {
      return <span />;
    }
    const { visible, onHide, tableInfo: { backupUUID } } = this.props;

    return (
      <div className="universe-apps-modal">
        <YBModal title={"Delete Backup"}
                 visible={visible}
                 onHide={onHide}
                 showCancelButton={true}
                 cancelLabel={"Cancel"}
                 onFormSubmit={this.confirmDeleteBackup}>
          Are you sure you want to delete this backup?
        </YBModal>
      </div>
    );
  }
}
