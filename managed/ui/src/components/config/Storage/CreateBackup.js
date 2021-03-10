// Copyright (c) YugaByte, Inc.
//
// Author: Nishant Sharma(nishant.sharma@hashedin.com)
//
// This file is responsible for the creation of all the input
// fields for it"s respective storage configuration type.

import { Field } from "redux-form";
import React from "react";
import { Col, Row } from "react-bootstrap";

const required = value => value ? undefined : "This field is required.";

const CreateBackup = (props) => {
  const {
    configName,
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
          validate={required}
        />
      </Col>
    </Row>
  )
}

export { CreateBackup }