// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import PropTypes from 'prop-types';
import { browserHistory } from 'react-router';
import { YBFormSelect } from '../../common/forms/fields';
import { isNonEmptyObject, isDefinedNotNull } from 'utils/ObjectUtils';
import { Field } from 'formik';
import { YBModalForm } from '../../common/forms';
import * as Yup from "yup";

export default class CreateBackup extends Component {
  static propTypes = {
    tableInfo: PropTypes.object
  };

  createBackup = values => {
    const {
      universeDetails: { universeUUID },
      onHide,
      createTableBackup,
      universeTables
    } = this.props;

    if (isDefinedNotNull(values.backupTableUUID) &&
        isDefinedNotNull(values.storageConfigUUID)) {
      const backupTable = universeTables.find((table) => table.tableUUID === values.backupTableUUID);
      const payload = {
        "storageConfigUUID": values.storageConfigUUID,
        "actionType": "CREATE",
        "tableName": backupTable.tableName,
        "keyspace": backupTable.keySpace
      };
      createTableBackup(universeUUID, backupTable.tableUUID, payload);
      onHide();
      browserHistory.push('/universes/' + universeUUID + "?tab=backups");
    }
  };

  render() {
    const { visible, onHide, tableInfo, storageConfigs, universeTables } = this.props;
    const storageOptions = storageConfigs.map((config) => {
      return {value: config.configUUID, label: config.name + " Storage"};
    });
    let tableOptions = [];
    let modalTitle = "Create Backup";
    if (isNonEmptyObject(tableInfo)) {
      tableOptions = [{
        value: tableInfo.tableID,
        label: tableInfo.keySpace + "." + tableInfo.tableName
      }];
      modalTitle = modalTitle + " for " + tableInfo.keySpace + "." + tableInfo.tableName;
    } else {
      tableOptions = universeTables.map((tableInfo) => {
        return {value: tableInfo.tableUUID, label: tableInfo.keySpace + "." + tableInfo.tableName};
      });
    }

    return (
      <div className="universe-apps-modal">
        <YBModalForm title={modalTitle}
                visible={visible}
                onHide={onHide}
                showCancelButton={true}
                cancelLabel={"Cancel"}
                onFormSubmit={(values) => {
                  const payload = {
                    ...values,
                    backupTableUUID: values.backupTableUUID.value,
                    storageConfigUUID: values.storageConfigUUID.value,
                  };
                  this.createBackup(payload);
                }}
                initialValues= {this.props.initialValues}
                validationSchema={
                  Yup.object().shape({
                    backupTableUUID: Yup.string()
                    .required('Backup Table is Required'),
                    storageConfigUUID: Yup.string()
                    .required('Storage Config is Required'),
                  })
                }>
          <Field name="storageConfigUUID" component={YBFormSelect}
                 label={"Storage"}
                 onInputChanged={this.storageConfigChanged}
                 options={storageOptions}  />
          <Field name="backupTableUUID" component={YBFormSelect}
                 label={"Table"} options={tableOptions}
                 onInputChanged={this.backupTableChanged} />
        </YBModalForm>
      </div>
    );
  }
}
