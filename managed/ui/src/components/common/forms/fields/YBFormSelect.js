// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { YBLabel } from 'components/common/descriptors';
import { isDefinedNotNull } from 'utils/ObjectUtils';

import Select from 'react-select';
import 'react-select/dist/react-select.css';

export default class YBFormSelect extends Component {
  handleChange = option => {
    const { form, field } = this.props;
    if (isDefinedNotNull(option)) {
      form.setFieldValue(field.name, option.value);
    }
  };

  handleBlur = () => {
    const { form, field } = this.props;
    form.setFieldTouched(field.name, true);
  };

  render() {
    return (
      <YBLabel {...this.props} >
        <Select
          {...this.props.field}
          {...this.props}
          onChange={this.handleChange}
          onBlur={this.handleBlur}
        />
      </YBLabel>
    );
  }
}
