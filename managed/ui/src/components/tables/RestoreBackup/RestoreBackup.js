// Copyright (c) YugaByte, Inc.

import React, { Component, Fragment } from 'react';
import PropTypes from 'prop-types';
import { browserHistory } from 'react-router';
import { YBFormSelect, YBFormInput } from '../../common/forms/fields';
import { getPromiseState } from '../../../utils/PromiseUtils';
import { isNonEmptyArray, isNonEmptyObject, isEmptyString } from 'utils/ObjectUtils';
import { YBModalForm } from '../../common/forms';
import { Field } from 'formik';
import * as Yup from "yup";

export default class RestoreBackup extends Component {
  static propTypes = {
    backupInfo: PropTypes.object
  };

  restoreBackup = values => {
    const {
      onHide,
      restoreTableBackup
    } = this.props;

    if (!isEmptyString(values.storageConfigUUID) &&
        !isEmptyString(values.storageLocation)) {
      const { restoreToUniverseUUID } = values;
      const payload = {
        storageConfigUUID: values.storageConfigUUID.value,
        storageLocation:  values.storageLocation,
        actionType: 'RESTORE',
        keyspace: values.restoreToKeyspace,
        tableName: values.restoreToTableName
      };
      onHide();
      restoreTableBackup(restoreToUniverseUUID.value, payload);
      browserHistory.push('/universes/' + restoreToUniverseUUID.value + "?tab=backups");
    }
  }
  hasBackupInfo = () => {
    return isNonEmptyObject(this.props.backupInfo);
  }

  render() {
    const { visible, onHide, universeList, storageConfigs, currentUniverse } = this.props;

    // If the backup information is not provided, most likely we are trying to load the backup
    // from pre-existing location (specified by the user) into the current universe in context.
    let universeOptions = [];
    let backupStorageInfo = null;
    const validationSchema = Yup.object().shape({
      restoreToUniverseUUID: Yup.string()
      .required('Restore To Universe is Required'),
      restoreToKeyspace: Yup.string()
      .required('Restore To Keyspace is Required'),
      restoreToTableName: Yup.string()
      .required('Restore To Tablename is Required'),
      storageConfigUUID: Yup.string()
      .required('Storage Config is Required'),
      storageLocation: Yup.string()
      .required('Storage Location is Required')
    });

    if (!this.hasBackupInfo()) {
      const storageOptions = storageConfigs.map((config) => {
        return {value: config.configUUID, label: config.name + " Storage"};
      });
      if (getPromiseState(currentUniverse).isSuccess() && isNonEmptyObject(currentUniverse.data)) {
        universeOptions = [{
          value: currentUniverse.data.universeUUID,
          label: currentUniverse.data.name}];
      }

      backupStorageInfo = (
        <Fragment>
          <Field name="storageConfigUUID" component={YBFormSelect}
                 label={"Storage"} options={storageOptions} />
          <Field name="storageLocation" component={YBFormInput}
                 componentClass="textarea"
                 className="storage-location"
                 label={"Storage Location"} />
        </Fragment>
      );
    } else {
      if (getPromiseState(universeList).isSuccess() && isNonEmptyArray(universeList.data)) {
        universeOptions = universeList.data.map((universe) => {
          return ({value: universe.universeUUID, label: universe.name});
        });
      }
    }

    return (
      <div className="universe-apps-modal">
        <YBModalForm title={"Restore backup To"}
                 visible={visible}
                 onHide={onHide}
                 showCancelButton={true}
                 cancelLabel={"Cancel"}
                 onFormSubmit={this.restoreBackup}
                 initialValues= {this.props.initialValues}
                 validationSchema={validationSchema}>
          {backupStorageInfo}
          <Field name="restoreToUniverseUUID" component={YBFormSelect}
                 label={"Universe"} options={universeOptions} />
          <Field name="restoreToKeyspace"
                 component={YBFormInput}
                 label={"Keyspace"} />
          <Field name="restoreToTableName"
                 component={YBFormInput}
                 label={"Table"}/>
        </YBModalForm>
      </div>
    );
  }
}
