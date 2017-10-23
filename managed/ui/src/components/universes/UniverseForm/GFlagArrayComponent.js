// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import {YBTextInput } from 'components/common/forms/fields';
import { Field, change, FieldArray } from 'redux-form';
import {Row, Col} from 'react-bootstrap';
import PropTypes from 'prop-types';

export default class GFlagArrayComponent extends Component {

  static propTypes = {
    type: PropTypes.oneOf(['master', 'tserver']).isRequired
  }

  constructor(props) {
    super(props);
    this.addRow = this.addRow.bind(this);
    this.removeRow = this.removeRow.bind(this);
  }

  componentWillMount() {
    if (this.props.fields.length == 0) {
      this.props.fields.push({});
    }
  }

  addRow() {
    this.props.fields.push({});
  }

  removeRow(idx) {
    this.props.fields.remove(idx);
  }

  render() {
    const {fields, type} = this.props;
    let self = this;
    let currentLabel = <span/>;
    if (type === "tserver") {
      currentLabel = "T-Server";
    } else {
      currentLabel = "Master";
    }
    return (
      <div>
        <label>{currentLabel}</label>
        {
          fields.map(function(field, idx){
            let isReadOnly = fields.get(idx).readOnly;
            return (
              <Row key={`${type}${idx}`} className="gflag-row">
                <Col md={5}>
                  <Field name={`${field}name`} component={YBTextInput} isReadOnly={isReadOnly}/>
                </Col>
                <Col md={5}>
                  <Field name={`${field}value`} component={YBTextInput} isReadOnly={isReadOnly}/>
                </Col>
                <Col md={2}>
                  {isReadOnly ? <span/> :
                    <i className="fa fa-minus-circle flag-button remove-flag-button" onClick={(idx) => self.removeRow}/>}
                </Col>
              </Row>
            );
          })
        }
        <Col md={12}>
          <i className="fa fa-plus-circle flag-button add-flag-button" onClick={self.addRow}/>
        </Col>
      </div>
    )
  }
}
