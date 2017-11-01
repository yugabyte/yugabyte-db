// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { YBTextInput } from 'components/common/forms/fields';
import { Field } from 'redux-form';
import { Row, Col } from 'react-bootstrap';
import PropTypes from 'prop-types';

export default class GFlagArrayComponent extends Component {

  static propTypes = {
    flagType: PropTypes.oneOf(['master', 'tserver']).isRequired,
    operationType: PropTypes.oneOf(['Create', 'Edit']).isRequired
  };

  constructor(props) {
    super(props);
    this.addRow = this.addRow.bind(this);
    this.removeRow = this.removeRow.bind(this);
  }

  componentWillMount() {
    if (this.props.fields.length === 0) {
      this.props.fields.push({});
    }
  }

  addRow() {
    const {operationType} = this.props;
    if (operationType !== "Edit") {
      this.props.fields.push({});
    }
  }

  removeRow(idx) {
    const {operationType} = this.props;
    if (operationType !== "Edit") {
      this.props.fields.remove(idx);
    }
  }

  render() {
    const {fields, flagType, operationType} = this.props;
    const isReadOnly = operationType === "Edit";
    const self = this;
    let currentLabel = <span/>;
    if (flagType === "tserver") {
      currentLabel = "T-Server";
    } else {
      currentLabel = "Master";
    }
    return (
      <div>
        <label>{currentLabel}</label>
        {
          fields.map(function(field, idx){
            return (
              <Row key={`${flagType}${idx}`} className="gflag-row">
                <Col md={5}>
                  <Field name={`${field}name`} component={YBTextInput} isReadOnly={isReadOnly}/>
                </Col>
                <Col md={5}>
                  <Field name={`${field}value`} component={YBTextInput} isReadOnly={isReadOnly}/>
                </Col>
                <Col md={2}>
                  {
                    isReadOnly ? <span/> :
                    <i className="fa fa-minus-circle flag-button remove-flag-button" onClick={() => self.removeRow(idx)}/>
                  }
                </Col>
              </Row>
            );
          })
        }
        <Col md={12}>
          {isReadOnly ? <span/>: <i className="fa fa-plus-circle flag-button add-flag-button" onClick={self.addRow}/>
          }
        </Col>
      </div>
    );
  }
}
