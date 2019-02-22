// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { YBLabel } from 'components/common/descriptors';
import { isDefinedNotNull } from 'utils/ObjectUtils';

import Select from 'react-select';

export default class YBFormSelect extends Component {
  handleChange = option => {
    const { form, field } = this.props;
    if (isDefinedNotNull(option)) {
      form.setFieldValue(field.name, option);
    } else {
      form.setFieldValue(field.name, "");
    }
  };

  handleBlur = () => {
    const { form, field } = this.props;
    form.setFieldTouched(field.name, true);
  };

  render() {
    const customStyles = {
      option: (provided, state) => ({
        ...provided,
        padding: 10,
      }),
      control: (provided) => ({
        // none of react-select's styles are passed to <Control />

        ...provided,
        width: "auto",
        borderColor: "#dedee0",
        borderRadius: 7,
        boxShadow: "inset 0 1px 1px rgba(0, 0, 0, .075)",
        fontSize: "14px",
        height: 42
      }),
      placeholder: (provided) => ({
        ...provided,
        color: "#999999"
      }),
      container: (provided) => ({
        ...provided,
      }),
      menuPortal: (provided) => ({
        ...provided,
        zIndex: 2
      }),
      dropdownIndicator: (provided) => ({
        ...provided,
        cursor: "pointer"
      }),
      clearIndicator: (provided) => ({
        ...provided,
        cursor: "pointer",
      }),
      singleValue: (provided, state) => {
        const opacity = state.isDisabled ? 0.5 : 1;
        const transition = 'opacity 300ms';
    
        return { ...provided, opacity, transition };
      }
    };
    
    return (
      <YBLabel {...this.props} >
        <Select
          className="Select"
          styles={customStyles}
          {...this.props.field}
          {...this.props}
          onChange={this.handleChange}
          onBlur={this.handleBlur}
        />
      </YBLabel>
    );
  }
}
