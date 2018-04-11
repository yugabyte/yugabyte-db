// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import PropTypes from 'prop-types';
import { browserHistory } from 'react-router';
import { Field } from 'redux-form';
import { YBModal, YBSelectWithLabel } from '../../common/forms/fields';
import { isNonEmptyObject, isDefinedNotNull } from '../../../utils/ObjectUtils';

export default class CreateBackup extends Component {
  static propTypes = {
    tableInfo: PropTypes.object
  };

  createBackup = values => {
    const {
      universeDetails: { universeUUID },
      tableInfo,
      onHide,
      createTableBackup,
      universeTables
    } = this.props;

    let payload = {
      "storageConfigUUID": values.storageConfigUUID,
      "actionType": "CREATE"
    };
    let tableUUID = null;
    if (isNonEmptyObject(tableInfo)) {
      payload = Object.assign(payload, {
        "tableName": tableInfo.tableName,
        "keyspace": tableInfo.keySpace
      });
      tableUUID = tableInfo.tableID;
    } else if (isDefinedNotNull(values.backupTableUUID)) {
      const backupTable = universeTables.find((table) => table.tableUUID === values.backupTableUUID);
      payload = Object.assign(payload, {
        "tableName": backupTable.tableName,
        "keyspace": backupTable.keySpace
      });
      tableUUID = backupTable.tableUUID;
    }
    createTableBackup(universeUUID, tableUUID, payload);
    onHide();
    browserHistory.push('/universes/' + universeUUID + "?tab=backups");
  };

  render() {
    const { visible, onHide, handleSubmit, tableInfo, customerConfigs, universeTables } = this.props;
    const storageOptions = customerConfigs.data.map((config, idx) => {
      return <option key={idx} value={config.configUUID}>{config.name + " Storage"}</option>;
    });
    let tableOptions = [];
    let modalTitle = "Create Backup";
    if (isNonEmptyObject(tableInfo)) {
      tableOptions = (
        <option key={tableInfo.tableID} value={tableInfo.tableID}>
          {tableInfo.keySpace + "." + tableInfo.tableName}
        </option>);
      modalTitle = modalTitle + " for " + tableInfo.keySpace + "." + tableInfo.tableName;
    } else {
      tableOptions = universeTables.map((tableInfo, idx) => {
        return (
          <option key={idx} value={tableInfo.tableUUID}>
            {tableInfo.keySpace + "." + tableInfo.tableName}
          </option>
        );
      });
    }

    return (
      <div className="universe-apps-modal">
        <YBModal title={modalTitle}
                 visible={visible}
                 onHide={onHide}
                 showCancelButton={true}
                 cancelLabel={"Cancel"}
                 onFormSubmit={handleSubmit(this.createBackup)}>
          <Field name="backupTableUUID" component={YBSelectWithLabel}
                 label={"Table"} options={tableOptions} />
          <Field name="storageConfigUUID" component={YBSelectWithLabel}
                 label={"Storage"} options={storageOptions} />
        </YBModal>
      </div>
    );
  }
}
