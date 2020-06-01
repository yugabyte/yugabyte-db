// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { YBTextInput } from '../../common/forms/fields';
import { Field } from 'redux-form';
import { Row, Col } from 'react-bootstrap';
import PropTypes from 'prop-types';
import { YBAddRowButton, YBRemoveRowButton } from '../../common/forms/fields';
import { FlexContainer, FlexShrink, FlexGrow } from '../../common/flexbox/YBFlexBox';

export default class GFlagArrayComponent extends Component {
  static propTypes = {
    flagType: PropTypes.oneOf(['master', 'tserver', 'tag']).isRequired,
    operationType: PropTypes.oneOf(['Create', 'Edit']).isRequired
  };

  componentDidMount() {
    if (this.props.fields.length === 0) {
      this.props.fields.push({});
    }
  }

  addRow = () => {
    const {operationType} = this.props;
    if (operationType !== "Edit") {
      this.props.fields.push({});
    }
  };

  removeRow = idx => {
    const {operationType} = this.props;
    if (operationType !== "Edit") {
      this.props.fields.remove(idx);
    }
  };

  render() {
    const {fields, flagType, isReadOnly} = this.props;

    const self = this;
    let currentLabel = false;
    if (flagType === "tserver") {
      currentLabel = "T-Server";
    } else if (flagType === "master") {
      currentLabel = "Master";
    }
    return (
      <div className="form-field-grid">
        {currentLabel && <label>{currentLabel}</label>}
        {
          fields.map(function(field, idx){
            return (
              <FlexContainer key={`${flagType}${idx}`} >
                <FlexGrow power={1}>
                  <Row className="gflag-row">
                    <Col xs={6}>
                      <div  className="yb-field-group">
                        <Field name={`${field}name`} component={YBTextInput} isReadOnly={isReadOnly}/>
                      </div>
                    </Col>
                    <Col xs={6}>
                      <div className="yb-field-group">
                        <Field name={`${field}value`} component={YBTextInput} isReadOnly={isReadOnly}/>
                      </div>
                    </Col>
                  </Row>
                </FlexGrow>
                <FlexShrink power={0} key={idx} className="form-right-control" style={isReadOnly ? {}: {marginRight:-10}}>
                  {
                    isReadOnly ? <span/> :
                    <YBRemoveRowButton onClick={() => self.removeRow(idx)}/>
                  }
                </FlexShrink>
              </FlexContainer>
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
