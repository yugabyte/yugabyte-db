// Copyright (c) YugaByte, Inc.
//
// Author: Nishant Sharma(nishant.sharma@hashedin.com)
//
// This file will hold a editable view for the particular storage
// configuration input fields.

import { Field } from 'redux-form';
import React from 'react';
import { Col, Row } from 'react-bootstrap';

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
  if (data.inUse && fieldId !== fieldName) {
    return true;
  }
};

export const EditBackupList = (props) => {
  const { configName, data, field } = props;

  return (
    <Row className="config-provider-row" key={configName + field.id}>
      <Col lg={2}>
        <div className="form-item-custom-label">{field.label}</div>
      </Col>
      <Col lg={10}>
        <Field
          name={field.id}
          placeHolder={field.placeHolder}
          component={field.component}
          isReadOnly={disableFields(data, field.id, configName)}
        />
      </Col>
    </Row>
  );
};
