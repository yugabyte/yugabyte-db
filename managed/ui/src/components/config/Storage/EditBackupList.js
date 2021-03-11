// Copyright (c) YugaByte, Inc.
//
// Author: Nishant Sharma(nishant.sharma@hashedin.com)
//
// This file will hold a editable view for the particular storage
// configuration input fields.

import { Field } from "redux-form";
import React from "react";
import { Col, Row } from "react-bootstrap";
import { isDefinedNotNull } from "../../../utils/ObjectUtils";
import { YBTextInputWithLabel } from "../../common/forms/fields";

// This method will handle the disable validation for the
// input fields.
const disbaleFields = (data, fieldId, configName) => {
  // data.inUse = true;
  if (data.inUse) {
    if (fieldId !== `${configName}_CONFIGURATION_NAME`) {
      return true;
    }
  }
}

const EditBackupList = (props) => {
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
          component={field.component}
          isReadOnly={disbaleFields(data, field.id, configName)}
        />
      </Col>
    </Row>
  )
}

export { EditBackupList }