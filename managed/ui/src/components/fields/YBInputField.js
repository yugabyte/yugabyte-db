// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import {FormControl} from 'react-bootstrap';
import YBLabel from './YBLabel';
import {isValidObject} from '../../utils/ObjectUtils';

export default class YBInputField extends Component {
  render() {
    var self = this;
    const { input, label, type, meta, className, placeHolder, onValueChanged } = this.props;
    var onChange = function(event) {
      if(isValidObject(onValueChanged) && typeof onValueChanged === "function") {
        onValueChanged(event.target.value);
      }
      self.props.input.onChange(event.target.value);
    }
    
    return (
      <YBLabel label={label} meta={meta}>
          <FormControl {...input} placeholder={placeHolder} type={type}
                            className={className} onChange={onChange}/>
      </YBLabel>
    )
  }
}



