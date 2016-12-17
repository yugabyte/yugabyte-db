// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import {isValidObject} from '../../../../utils/ObjectUtils';
import { YBLabel } from '../../descriptors';

export default class YBSelect extends Component {
  render() {
    const { input, label, meta, options, onSelectChange, readOnlySelect, name} = this.props;
    var onChange = function(event) {
      input.onChange(event.target.value);
      if (isValidObject(onSelectChange)) {
        onSelectChange(event.target.value);
      }
    }
    return (
      <YBLabel label={label} meta={meta}>
          <select {...input} name={name} className="form-control"
                  disabled={readOnlySelect} onChange={onChange}
                 >
            {options}
          </select>
      </YBLabel>
    )
  }
}
