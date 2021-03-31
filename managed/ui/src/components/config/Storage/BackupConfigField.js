// Copyright (c) YugaByte, Inc.
//
// Author: Nishant Sharma(nishant.sharma@hashedin.com)
//
// This file will hold a editable view for the particular storage
// configuration input fields.

import { Field } from 'redux-form';
import React from 'react';
import { Col, Row } from 'react-bootstrap';
import YBInfoTip from '../../common/descriptors/YBInfoTip';
import { isEmptyObject } from '../../../utils/ObjectUtils';

/**
 * This method will handle the validation for current
 * field.
 *
 * @param {any} value Input field value.
 */
const required = (value) => (value ? undefined : 'This field is required.');

/**
 * This method is used to handle the edit backup storage
 * config form.
 *
 * @param {object} data Respective row details.
 * @param {string} fieldId Input field id.
 * @param {string} configName Input field name.
 * @returns inUse ? true : false;
 */
const disableFields = (data, fieldId, configName) => {
  const fieldName = `${configName}_CONFIGURATION_NAME`;
  if (!isEmptyObject(data) && fieldId !== fieldName) {
    return true;
  }
};

export const BackupConfigField = (props) => {
  const { configName, data, field } = props;

  // This is the tool tip for all the backup configuration names.
  const configNameToolTip = field.label === "Configuration Name"
    ? <Col lg={1} className="config-zone-tooltip">
      <YBInfoTip
        title="Configuration Name"
        content="The backup configuration name is required."
      />
    </Col> : "";

  return (
    <Row className="config-provider-row" key={configName + field.id}>
      <Col lg={2}>
        <div className="form-item-custom-label">{field.label}</div>
      </Col>
      <Col lg={9}>
        <Field
          name={field.id}
          placeHolder={field.placeHolder}
          component={field.component}
          validate={required}
          isReadOnly={disableFields(data, field.id, configName)}
        />
      </Col>
      {configNameToolTip}
    </Row>
  );
};
