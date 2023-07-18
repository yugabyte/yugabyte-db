// Copyright (c) YugaByte, Inc.

import { Component } from 'react';
import { YBLabel } from '../../../../components/common/descriptors';
import { FormControl } from 'react-bootstrap';
import { isDefinedNotNull, trimString } from '../../../../utils/ObjectUtils';
import { isFunction } from 'lodash';
export default class YBFormInput extends Component {
  handleChange = (event) => {
    const { field, onChange } = this.props;
    field.onChange(event);
    if (isDefinedNotNull(onChange)) onChange(this.props, event);
  };

  handleBlur = (event) => {
    const { field, onBlur } = this.props;
    if (isDefinedNotNull(field) && isFunction(field.onBlur)) {
      event.target.value = trimString(event.target.value);
      field.onChange(event);
      field.onBlur(event);
    }
    if (isDefinedNotNull(onBlur)) onBlur(event);
  };

  render() {
    const { field, infoContent, ...rest } = this.props;

    return (
      <YBLabel {...this.props} infoContent={infoContent}>
        <FormControl {...field} {...rest} onChange={this.handleChange} onBlur={this.handleBlur} />
      </YBLabel>
    );
  }
}
