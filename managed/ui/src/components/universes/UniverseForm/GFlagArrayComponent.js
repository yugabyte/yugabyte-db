// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { YBTextInput } from 'components/common/forms/fields';
import { Field } from 'redux-form';
import { Row, Col } from 'react-bootstrap';
import PropTypes from 'prop-types';
import { YBAddRowButton, YBRemoveRowButton } from '../../common/forms/fields';

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
      <div className="form-field-grid">
        <label>{currentLabel}</label>
        {
          fields.map(function(field, idx){
            return (
              <Row key={`${flagType}${idx}`} className="gflag-row">
                <Col md={5}>
                  <div  className="yb-field-group">
                    <Field name={`${field}name`} component={YBTextInput} isReadOnly={isReadOnly}/>
                  </div>
                </Col>
                <Col md={5}>
                  <div className="yb-field-group">
                    <Field name={`${field}value`} component={YBTextInput} isReadOnly={isReadOnly}/>
                  </div>
                </Col>
                <Col md={2}>
                  {
                    isReadOnly ? <span/> :
                    <YBRemoveRowButton onClick={() => self.removeRow(idx)}/>
                  }
                </Col>
              </Row>
            );
          })
        }
        <Row>
          <Col md={12}>
            {
              isReadOnly ? <span/>: <YBAddRowButton btnText="Add Row" onClick={self.addRow}/>
            }
          </Col>
        </Row>
      </div>
    );
  }
}
