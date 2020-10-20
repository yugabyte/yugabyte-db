// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import PropTypes from 'prop-types';
import { browserHistory } from 'react-router';
import { YBFormSelect, YBFormInput } from '../../common/forms/fields';
import { getPromiseState } from '../../../utils/PromiseUtils';
import { isNonEmptyArray, isNonEmptyObject, isEmptyString } from '../../../utils/ObjectUtils';
import { YBModalForm } from '../../common/forms';
import { Field } from 'formik';
import * as Yup from "yup";

export default class RestoreBackup extends Component {
  static propTypes = {
    backupInfo: PropTypes.object
  };

  restoreBackup = async values => {
    const {
      onHide,
      onSubmit,
      onError,
      restoreTableBackup,
      initialValues
    } = this.props;

    if (!isEmptyString(values.storageConfigUUID) &&
        (!isEmptyString(values.storageLocation) || values.backupList)) {
      const { restoreToUniverseUUID } = values;
      const payload = {
        storageConfigUUID: values.storageConfigUUID,
        storageLocation:  values.storageLocation,
        actionType: 'RESTORE',
        parallelism: values.parallelism
      };
      if (values.backupList) {
        payload.backupList = values.backupList;
      } else if (values.restoreToTableName !== initialValues.restoreToTableName) {
        payload.tableName = values.restoreToTableName;
        payload.keyspace = values.restoreToKeyspace;
      } else if (values.restoreToKeyspace !== initialValues.restoreToKeyspace) {
        payload.keyspace = values.restoreToKeyspace;
      }

      try {
        const response = await restoreTableBackup(restoreToUniverseUUID, payload);        
        onSubmit(response.data);
      } catch (err) {
        if (onError) {
          onError();
        }
      }
      onHide();
      browserHistory.push('/universes/' + restoreToUniverseUUID + "/backups");
    }
  }
  hasBackupInfo = () => {
    return isNonEmptyObject(this.props.backupInfo);
  }

  render() {
    const { backupInfo, visible, onHide, universeList, storageConfigs, currentUniverse } = this.props;

    // If the backup information is not provided, most likely we are trying to load the backup
    // from pre-existing location (specified by the user) into the current universe in context.
    let universeOptions = [];
    const hasBackupInfo = this.hasBackupInfo();
    const validationSchema = Yup.object().shape({
      restoreToUniverseUUID: Yup.string().required('Restore To Universe is Required'),
      restoreToKeyspace: Yup.string().nullable(),
      restoreToTableName: Yup.string().nullable(),
      storageConfigUUID: Yup.string().required('Storage Config is Required'),
      storageLocation: Yup.string().nullable()
        .when('backupList', {
          is: x => !x || !Array.isArray(x),
          then: Yup.string().required('Storage Location is Required'),
        }),
      backupList: Yup.mixed().nullable(),
      parallelism: Yup.number('Parallelism must be a number')
        .min(1)
        .max(100)
        .integer('Value must be a whole number')
        .required('Number of threads is required')
    });

    if (hasBackupInfo) {
      if (getPromiseState(universeList).isSuccess() && isNonEmptyArray(universeList.data)) {
        universeOptions = universeList.data.map((universe) => {
          return ({value: universe.universeUUID, label: universe.name});
        });
      }
    } else {
      if (getPromiseState(currentUniverse).isSuccess() && isNonEmptyObject(currentUniverse.data)) {
        universeOptions = [{
          value: currentUniverse.data.universeUUID,
          label: currentUniverse.data.name}];
      }
    }

    const storageOptions = storageConfigs.map((config) => {
      return {value: config.configUUID, label: config.name + " Storage"};
    });

    const initialValues = {
      ...this.props.initialValues,
      storageConfigUUID: hasBackupInfo ? storageOptions.find((element) => { return element.value === this.props.initialValues.storageConfigUUID;}) : ""
    };
    const isUniverseBackup = hasBackupInfo && Array.isArray(backupInfo.backupList) && backupInfo.backupList.length;

    // Disable table field if multi-table backup
    const isMultiTableBackup = hasBackupInfo && (
      isUniverseBackup ||
      (backupInfo.tableNameList && backupInfo.tableNameList.length > 1) ||
      (backupInfo.keyspace && (!backupInfo.tableNameList || !backupInfo.tableNameList.length) && !backupInfo.tableUUID)
    );

    return (
      <div className="universe-apps-modal" onClick={(e) => e.stopPropagation()}>
        <YBModalForm title={"Restore data to"}
                visible={visible}
                onHide={onHide}
                showCancelButton={true}
                cancelLabel={"Cancel"}
                onFormSubmit={(values) => {
                  let restoreToUniverseUUID = values.restoreToUniverseUUID;

                  // Check if not default value
                  if (typeof restoreToUniverseUUID !== 'string') {
                    restoreToUniverseUUID = values.restoreToUniverseUUID.value;
                  }
                  const payload = {
                    ...values,
                    restoreToUniverseUUID,
                    storageConfigUUID: values.storageConfigUUID.value,
                  };
                  if (values.storageLocation) {
                    payload.storageLocation = values.storageLocation.trim()
                  }
                  this.restoreBackup(payload);
                }}
                initialValues= {initialValues}
                validationSchema={validationSchema}>

          <Field name="storageConfigUUID" {...(hasBackupInfo ? {type: "hidden"} : null)} component={YBFormSelect}
                 label={"Storage"} options={storageOptions} />
          <Field name="storageLocation" {...(hasBackupInfo ? {type: "hidden"} : null)} component={YBFormInput}
                 componentClass="textarea"
                 className="storage-location"
                 label={"Storage Location"} />
          <Field name="restoreToUniverseUUID" component={YBFormSelect}
                label={"Universe"} options={universeOptions} />
          <Field name="restoreToKeyspace"
                component={YBFormInput}
                disabled={isUniverseBackup}
                label={"Keyspace"} />
          <Field name="restoreToTableName"
                component={YBFormInput}
                disabled={isMultiTableBackup}
                label={"Table"}/>
          <Field
              name="parallelism"
              component={YBFormInput}
              label={"Parallel Threads"}/>
        </YBModalForm>
      </div>
    );
  }
}
