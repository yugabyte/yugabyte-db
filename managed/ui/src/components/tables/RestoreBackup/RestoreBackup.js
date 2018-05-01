// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import PropTypes from 'prop-types';
import { Field, change } from 'redux-form';
import { YBModal, YBSelectWithLabel, YBTextInputWithLabel } from '../../common/forms/fields';
import { getPromiseState } from '../../../utils/PromiseUtils';
import { isNonEmptyArray, isNonEmptyObject, isDefinedNotNull } from 'utils/ObjectUtils';

export default class RestoreBackup extends Component {
  static propTypes = {
    backupInfo: PropTypes.object
  };

  constructor(props) {
    super(props);
    this.updateFormField = this.updateFormField.bind(this);
  }

  updateFormField = (field, value) => {
    this.props.dispatch(change("restoreBackupForm", field, value));
  };

  restoreBackup = values => {
    const {
      backupInfo : {backupUUID, storageConfigUUID, storageLocation },
      onHide,
      restoreTableBackup
    } = this.props;
    const { restoreUniverseUUID, restoreKeyspace, restoreTableName } = values;
    if (isDefinedNotNull(restoreKeyspace) &&
        isDefinedNotNull(restoreTableName) &&
        isDefinedNotNull(restoreUniverseUUID)) {
      const payload = {
        keyspace: restoreKeyspace,
        tableName: restoreTableName,
        storageConfigUUID: storageConfigUUID,
        storageLocation: storageLocation,
        actionType: "RESTORE"
      };
      onHide();
      restoreTableBackup(restoreUniverseUUID, backupUUID, payload);
    }
  }

  componentDidUpdate(prevProps, prevState, snapshot) {
    if (isNonEmptyObject(this.props.backupInfo) &&
        this.props.backupInfo !== prevProps.backupInfo) {
      const { backupInfo : { universeUUID, keyspace, tableName }} = this.props;
      this.updateFormField("restoreUniverseUUID", universeUUID);
      this.updateFormField("restoreTableName", tableName);
      this.updateFormField("restoreKeyspace", keyspace);
    }
  }

  render() {
    const { visible, onHide, handleSubmit, universeList } = this.props;
    let universeOptions = [];
    if (getPromiseState(universeList).isSuccess() && isNonEmptyArray(universeList.data)) {
      universeOptions = universeList.data.map((universe, idx) => {
        return (
          <option key={idx} value={universe.universeUUID} >
            {universe.name}
          </option>
        );
      });
    }

    return (
      <div className="universe-apps-modal">
        <YBModal title={"Restore backup To"}
                 visible={visible}
                 onHide={onHide}
                 showCancelButton={true}
                 cancelLabel={"Cancel"}
                 onFormSubmit={handleSubmit(this.restoreBackup)}>
          <Field name="restoreUniverseUUID" component={YBSelectWithLabel}
                 label={"Universe"} options={universeOptions} />
          <Field name="restoreKeyspace"
                 component={YBTextInputWithLabel}
                 label={"Keyspace"} />
          <Field name="restoreTableName"
                 component={YBTextInputWithLabel}
                 label={"Table"}/>
        </YBModal>
      </div>
    );
  }
}
