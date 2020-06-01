// Copyright (c) YugaByte, Inc.

import React, { Component, Fragment } from 'react';
import PropTypes from 'prop-types';
import { components } from 'react-select';
import { browserHistory } from 'react-router';
import { YBFormSelect, YBFormToggle, YBFormInput } from '../../common/forms/fields';
import YBInfoTip from '../../common/descriptors/YBInfoTip';
import { Row, Col } from 'react-bootstrap';
import {
  isNonEmptyObject,
  isDefinedNotNull,
  isNonEmptyArray,
  isEmptyString,
  isNonEmptyString
} from '../../../utils/ObjectUtils';
import { Field } from 'formik';
import { YBModalForm } from '../../common/forms';
import * as cron from 'cron-validator';
import * as Yup from "yup";

import '../common.scss';

const schemaValidation =  Yup.object().shape({
  backupTableUUID: Yup.string().when('tableKeyspace', {
    is: (tableKeyspace) => !tableKeyspace,
    then: Yup.string().required('Backup keyspace or table is required')
  }),
  tableKeyspace: Yup.string().when('backupTableUUID', {
    is: (backupTableUUID) => !backupTableUUID,
    then: Yup.string().required('Backup keyspace or table is required')
  }),
  storageConfigUUID: Yup.string()
  .required('Storage Config is Required'),
  enableSSE: Yup.bool(),
  schedulingFrequency: Yup.number('Frequency must be a number'),
  cronExpression: Yup.string().test({
    name: "isValidCron",
    test: (value) => (value && cron.isValidCron(value)) || !value,
    message: 'Does not looks like a valid cron expression'
  })
}, ['backupTableUUID', 'tableKeyspace']);

export default class CreateBackup extends Component {
  static propTypes = {
    tableInfo: PropTypes.object
  };

  createBackup = values => {
    const {
      universeDetails: { universeUUID },
      onHide,
      createTableBackup,
      createUniverseBackup,
      universeTables
    } = this.props;

    if (isDefinedNotNull(values.storageConfigUUID)) {
      const payload = {
        "storageConfigUUID": values.storageConfigUUID,
        "sse": values.enableSSE,
        "schedulingFrequency": isEmptyString(values.schedulingFrequency) ? null : values.schedulingFrequency,
        "cronExpression": isNonEmptyString(values.cronExpression) ? values.cronExpression : null,
      };
      if (isDefinedNotNull(values.backupTableUUID) && values.backupTableUUID.length) {
        if (values.backupTableUUID[0] === "fulluniverse") {
          createUniverseBackup(universeUUID, payload);
        } else if (values.backupTableUUID.length > 1) {
          payload.tableUUIDList = values.backupTableUUID;
          createUniverseBackup(universeUUID, payload);
        } else {
          const backupTable = universeTables
                                .find((table) => table.tableUUID === values.backupTableUUID[0]);
          payload.tableName = backupTable.tableName;
          payload.keyspace = backupTable.keySpace;
          payload.actionType = "CREATE";
          createTableBackup(universeUUID, values.backupTableUUID, payload);
        }
      } else if (values.tableKeyspace) {
        payload.keyspace = values.tableKeyspace.value;
        createUniverseBackup(universeUUID, payload);
      }
      onHide();
      browserHistory.push('/universes/' + universeUUID + "/backups");
    }
  };

  backupTableChanged = (props, option) => {
    if (isNonEmptyObject(option) && option.value === "fulluniverse") {
      props.form.setFieldValue(props.field.name, option);
    } else if (isNonEmptyArray(option)) {
      const index = option.findIndex((item, index) => item.value === "fulluniverse");
      if (index > -1) {
        // Clear all other values except 'Full Universe Backup'
        props.form.setFieldValue(props.field.name, [option[index]]);
      } else {
        props.form.setFieldValue(props.field.name, option);
      }
    } else {
      // Clear form
      props.form.setFieldValue(props.field.name, []);
    }
  }

  render() {
    const { visible, isScheduled, onHide, tableInfo, storageConfigs, universeTables } = this.props;
    const storageOptions = storageConfigs.map((config) => {
      return {value: config.configUUID, label: config.name + " Storage"};
    });
    const initialValues = this.props.initialValues;

    let tableOptions = [];
    const keyspaceOptions = [{ value: '', label: 'None'}];
    let modalTitle = "Create Backup";
    if (isNonEmptyObject(tableInfo)) {
      tableOptions = [{
        value: tableInfo.tableID,
        label: tableInfo.keySpace + "." + tableInfo.tableName
      }];
      modalTitle = modalTitle + " for " + tableInfo.keySpace + "." + tableInfo.tableName;
      initialValues.backupTableUUID = tableOptions[0];
    } else {
      const keyspaces = new Set();
      tableOptions = universeTables.map((tableInfo) => {
        keyspaces.add(tableInfo.keySpace);
        return {value: tableInfo.tableUUID, label: tableInfo.keySpace + "." + tableInfo.tableName};
      }).sort((a, b) => a.label.toLowerCase() < b.label.toLowerCase() ? -1 : 1);
      tableOptions = [
        {
          label: <b>Full Universe Backup</b>,
          value: "fulluniverse",
          icon: <span className={"fa fa-globe"} />
        },
        {
          label: "Tables",
          value: 'tables',
          options: tableOptions
        }
      ];
      keyspaces.forEach(key => {
        keyspaceOptions.push({ value: key, label: key});
      });
    }

    initialValues.schedulingFrequency = "";
    initialValues.cronExpression = "";

    const customOption = (props) => (<components.Option {...props}>
      <div className="input-select__option">
        { props.data.icon && <span className="input-select__option-icon">{ props.data.icon }</span> }
        <span>{ props.data.label }</span>
      </div>
    </components.Option>);

    const customSingleValue = (props) => (<components.SingleValue {...props}>
      { props.data.icon && <span className="input-select__single-value-icon">{ props.data.icon }</span> }
      <span>{ props.data.label }</span>
    </components.SingleValue>);

    return (
      <div className="universe-apps-modal">
        <YBModalForm
          title={modalTitle}
          visible={visible}
          onHide={onHide}
          showCancelButton={true}
          cancelLabel={"Cancel"}
          onFormSubmit={(values) => {
            const payload = {
              ...values,
              backupTableUUID: Array.isArray(values.backupTableUUID) ? 
                values.backupTableUUID.map(x => x.value) : [values.backupTableUUID.value],
              storageConfigUUID: values.storageConfigUUID.value,
            };
            this.createBackup(payload);
          }}
          initialValues={initialValues}
          validationSchema={schemaValidation}
          render={({values: { cronExpression, schedulingFrequency, backupTableUUID, storageConfigUUID }}) => {
            const isSchedulingFrequencyReadOnly = cronExpression !== "";
            const isCronExpressionReadOnly = schedulingFrequency !== "";
            const isTableSelected = backupTableUUID && backupTableUUID.length && backupTableUUID[0].value !== 'fulluniverse';
            const s3StorageSelected = storageConfigUUID && storageConfigUUID.label === 'S3 Storage';

            // params for backupTableUUID <Field>
            // NOTE: No entire keyspace selection implemented
            return (<Fragment>
              {isScheduled &&
                <div className="backup-frequency-control">                  
                  <Row>
                    <Col xs={6}>
                      <Field
                        name="schedulingFrequency"
                        component={YBFormInput}
                        readOnly={isSchedulingFrequencyReadOnly}
                        type={"number"}
                        label={"Backup frequency"}
                        placeholder={"Interval in ms"}
                      />
                    </Col>
                  </Row>
                  <div className="separating-text">OR</div>
                  <Row>
                    <Col xs={6}>
                      <Field
                        name="cronExpression"
                        component={YBFormInput}
                        readOnly={isCronExpressionReadOnly}
                        placeholder={"Cron expression"}
                        label={"Cron expression"}
                      />
                    </Col>
                    <Col lg={1} className="cron-expr-tooltip">
                      <YBInfoTip title="Cron Expression Format"
                        content={<div><code>Min&nbsp; Hour&nbsp; Day&nbsp; Mon&nbsp; Weekday</code>
                          <pre><code>*    *    *    *    *  command to be executed</code></pre>
                          <pre><code>┬    ┬    ┬    ┬    ┬<br />
│    │    │    │    └─  Weekday  (0=Sun .. 6=Sat)<br />
│    │    │    └──────  Month    (1..12)<br />
│    │    └───────────  Day      (1..31)<br />
│    └────────────────  Hour     (0..23)<br />
└─────────────────────  Minute   (0..59)</code></pre>
                        </div>} />  
                    </Col>
                  </Row>
                </div>
              }
              <Field
                name="storageConfigUUID"
                component={YBFormSelect}
                label={"Storage"}
                options={storageOptions}
              />
              {!!keyspaceOptions.length &&
                <Field
                  name="tableKeyspace"
                  component={YBFormSelect}
                  label="Table keyspace"
                  options={keyspaceOptions}
                  isDisabled={isTableSelected || tableInfo} // Disable if backup table is specified
                />
              }
              <Field
                name="backupTableUUID"
                component={YBFormSelect}
                components={{
                  Option: customOption,
                  SingleValue: customSingleValue
                }}
                label={`Tables to backup`}
                options={tableOptions}
                isMulti={true}
                onChange={this.backupTableChanged}
                readOnly={isNonEmptyObject(tableInfo)}
              />
              {s3StorageSelected && <Field
                name="enableSSE"
                component={YBFormToggle}
                label={"Enable Server-Side Encryption"}
              />}
            </Fragment>);
          }}
        />
      </div>
    );
  }
}
