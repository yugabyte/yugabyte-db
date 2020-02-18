// Copyright (c) YugaByte, Inc.

import React, { Component, Fragment } from 'react';
import PropTypes from 'prop-types';
import { components } from 'react-select';
import { browserHistory } from 'react-router';
import { YBFormSelect, YBFormToggle, YBFormInput } from '../../common/forms/fields';
import { Row, Col } from 'react-bootstrap';
import { isNonEmptyObject, isDefinedNotNull, isNonEmptyArray, isEmptyString, isNonEmptyString } from 'utils/ObjectUtils';
import { Field } from 'formik';
import { YBModalForm } from '../../common/forms';
import * as cron from 'cron-validator';
import * as Yup from "yup";

import '../common.scss';

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

    if (isDefinedNotNull(values.backupTableUUID) &&
        values.backupTableUUID.length &&
        isDefinedNotNull(values.storageConfigUUID)) {
      const payload = {
        "storageConfigUUID": values.storageConfigUUID,
        "sse": values.enableSSE,
        "schedulingFrequency": isEmptyString(values.schedulingFrequency) ? null : values.schedulingFrequency,
        "cronExpression": isNonEmptyString(values.cronExpression) ? values.cronExpression : null,
      };
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
      onHide();
      browserHistory.push('/universes/' + universeUUID + "/backups");
    }
  };

  backupItemChanged = (props, option) => {
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
    const { visible, onHide, tableInfo, storageConfigs, universeTables } = this.props;
    const storageOptions = storageConfigs.map((config) => {
      return {value: config.configUUID, label: config.name + " Storage"};
    });
    const initialValues = this.props.initialValues;
    let tableOptions = [];
    let modalTitle = "Create Backup";
    if (isNonEmptyObject(tableInfo)) {
      tableOptions = [{
        value: tableInfo.tableID,
        label: tableInfo.keySpace + "." + tableInfo.tableName
      }];
      modalTitle = modalTitle + " for " + tableInfo.keySpace + "." + tableInfo.tableName;
      initialValues.backupTableUUID = tableOptions[0];
    } else {
      tableOptions = universeTables.map((tableInfo) => {
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
              backupTableUUID: values.backupTableUUID.map(x => x.value),
              storageConfigUUID: values.storageConfigUUID.value,
            };
            this.createBackup(payload);
          }}
          initialValues={initialValues}
          validationSchema={
            Yup.object().shape({
              backupTableUUID: Yup.string()
              .required('Backup Table is Required'),
              storageConfigUUID: Yup.string()
              .required('Storage Config is Required'),
              enableSSE: Yup.bool(),
              schedulingFrequency: Yup.number('Frequency must be a number'),
              cronExpression: Yup.string().test({
                name: "isValidCron",
                test: (value) => (value && cron.isValidCron(value)) || !value,
                message: 'Does not looks like a valid cron expression'
              })
            })
          }
          render={props => {
            const isSchedulingFrequencyReadOnly = props.values.cronExpression !== "";
            const isCronExpressionReadOnly = props.values.schedulingFrequency !== "";

            // params for backupTableUUID <Field>
            // NOTE: No entire keyspace selection implemented
            return (<Fragment>
              <Field
                name="storageConfigUUID"
                component={YBFormSelect}
                label={"Storage"}
                onInputChanged={this.storageConfigChanged}
                options={storageOptions}
              />
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
                onChange={this.backupItemChanged}
                readOnly={isNonEmptyObject(tableInfo)}
              />
              <Field
                name="enableSSE"
                component={YBFormToggle}
                label={"Enable Server-Side Encryption"}
              />
              <div className="backup-frequency-control">
                <span>or</span>
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
                  <Col xs={6}>
                    <Field
                      name="cronExpression"
                      component={YBFormInput}
                      readOnly={isCronExpressionReadOnly}
                      label={" "}
                      placeholder={"Cron expression"}
                    />
                  </Col>
                </Row>
              </div>
            </Fragment>);
          }}
        />
      </div>
    );
  }
}
