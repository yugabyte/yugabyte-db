// Copyright (c) YugaByte, Inc.
// 
// Author: Nishant Sharma(nishant.sharma@hashedin.com)

import { Field } from 'redux-form';
import React from 'react';
import { Col, Row } from 'react-bootstrap';
import { isDefinedNotNull } from '../../../utils/ObjectUtils';
import { YBTextInputWithLabel } from '../../common/forms/fields';

// This method will handle the disable validation for the
// input fields.
const enableFields = (data, fieldId, configName) => {
  if (data.inUse) {
    if (fieldId !== `${configName}_CONFIGURATION_NAME`) {
      return true;
    }
  }
}

function EditBackupList(props) {
  const {
    configName,
    data,
    field
  } = props;

  return (
    <Row className="config-provider-row" key={configName + field.id}>
      <Col lg={2}>
        <div className="form-item-custom-label">{field.label}</div>
      </Col>
      <Col lg={10}>
        <Field
          name={field.id}
          placeHolder={field.placeHolder}
          input={{
            value: data[field.id],
            disabled: enableFields(data, field.id, configName)
          }}
          component={YBTextInputWithLabel}
        />
      </Col>
    </Row>
  )
}

export { EditBackupList }