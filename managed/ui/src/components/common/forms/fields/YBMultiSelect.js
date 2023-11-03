// Copyright (c) YugaByte, Inc.

import { Component } from 'react';
import { isFunction } from 'lodash';
import Select, { components } from 'react-select';
import makeAnimated from 'react-select/animated';

import { YBLabel } from '../../../../components/common/descriptors';
import { isNonEmptyArray } from '../../../../utils/ObjectUtils';
import clearIcon from '../../media/x-large.svg';

const colourStyles = {
  option: (styles, { isFocused }) => ({
    ...styles,
    backgroundColor: 'white',
    color: isFocused ? '#EF5824' : '#333333'
  }),
  control: (styles) => ({
    ...styles,
    borderRadius: '8px',
    paddingLeft: '10px',
    height: '40px',
    alignContent: 'center'
  }),
  valueContainer: (styles, { hasValue }) => ({
    ...styles,
    padding: hasValue ? '5px 0px 5px 8px' : '0',
    width: 'auto'
  }),
  singleValue: (styles) => ({
    ...styles,
    backgroundColor: '#E5E5E9',
    borderRadius: '6px',
    padding: '5px 8px',
    maxWidth: 'calc(100% - 10px)',
    position: 'relative',
    top: 'initial',
    transform: 'none'
  }),
  multiValue: (styles) => ({
    ...styles,
    backgroundColor: '#E5E5E9',
    borderRadius: '6px',
    margin: '6px 1px 6px 1px'
  }),
  multiValueLabel: (styles) => ({
    ...styles,
    padding: '7.5px 0px 7.5px 11px !important',
    borderRadius: '6px',
    fontFamily: 'Inter',
    fontStyle: 'normal',
    fontWeight: '500',
    fontSize: '12px'
  }),
  multiValueRemove: (styles) => ({
    ...styles,
    borderRadius: '0 6px 6px 0',
    padding: '1px 7px 0 2px',
    marginLeft: '2.5px',
    ':hover': {
      backgroundColor: '#E5E5E9'
    },
    cursor: 'pointer'
  }),
  menu: (base) => ({
    ...base,
    width: 'max-content',
    minWidth: '100%'
  }),
  dropdownIndicator: (styles) => ({
    ...styles,
    cursor: 'pointer',
    paddingLeft: '5px'
  }),
  clearIndicator: (styles) => ({
    ...styles,
    cursor: 'pointer',
    padding: 0
  })
};

const Control = ({ children, hasValue, menuIsOpen, ...props }) => {
  const labelStyle = {
    fontFamily: 'Inter',
    fontStyle: 'normal',
    fontSize: '14px'
  };
  const {
    selectProps: { customLabel }
  } = props;

  return (
    <components.Control {...props}>
      {hasValue && customLabel && <span style={labelStyle}>{customLabel}</span>}
      {children}
    </components.Control>
  );
};

const MultiValue = (props) => {
  return <components.MultiValue className="YBMultiValue" {...props} />;
};
const MultiValueRemove = (props) => {
  return (
    <components.MultiValueRemove {...props}>
      <img src={clearIcon} alt="clear" className="clear-icon" style={{ height: 16, width: 16 }} />
    </components.MultiValueRemove>
  );
};

const SingleValue = (props) => {
  return <components.SingleValue className="YBSingleValue" {...props} />;
};

const DropdownIndicator = (props) => {
  const indicatorStyle = {
    width: 0,
    height: 0,
    borderLeft: '4px solid transparent',
    borderRight: '4px solid transparent',
    borderTop: '4px solid black'
  };

  return (
    <components.DropdownIndicator {...props}>
      <div style={indicatorStyle} />
    </components.DropdownIndicator>
  );
};

const ClearIndicator = (props) => {
  return (
    <components.ClearIndicator {...props}>
      <img
        src={clearIcon}
        alt="clear all"
        className="clear-icon lg-icon"
        style={{ height: 24, width: 24 }}
      />
    </components.ClearIndicator>
  );
};

const animatedComponents = makeAnimated({
  Control,
  MultiValue,
  MultiValueRemove,
  DropdownIndicator,
  ClearIndicator,
  IndicatorSeparator: () => null,
  SingleValue
});

export const YBMultiSelectRedesiged = (props) => {
  const {
    options,
    value,
    onChange,
    placeholder,
    name,
    className,
    isMulti = true,
    customLabel,
    isClearable = false
  } = props;
  return (
    <Select
      className={className}
      classNamePrefix="select"
      isMulti={isMulti}
      name={name}
      placeholder={placeholder}
      options={options}
      value={value}
      onChange={onChange}
      components={animatedComponents}
      styles={colourStyles}
      customLabel={customLabel}
      isClearable={isClearable}
    />
  );
};

// The below is used in all our redux-form.
// TODO: Unify the design for all dropdowns and reduce some code duplication here.
// ----------------------------------------------------------------------------
// TODO: Rename to YBMultiSelect after changing prior YBMultiSelect references.
// TODO: Make default export after checking all corresponding imports.
export class YBNewMultiSelect extends Component {
  componentDidUpdate(prevProps) {
    let newSelection = null;

    // If AZ is changed from multi to single, take only last selection.
    if (this.props.isMulti !== prevProps.isMulti && this.props.isMulti === false) {
      const currentSelection = prevProps.input.value;
      if (isNonEmptyArray(currentSelection)) {
        newSelection = currentSelection.splice(-1, 1);
      }
    }
    // If provider has been changed, reset region selection.
    if (this.props.providerSelected !== prevProps.providerSelected) {
      newSelection = [];
    }

    if (isNonEmptyArray(newSelection) && isFunction(this.props.input.onChange)) {
      this.props.input.onChange(newSelection);
    }
  }

  render() {
    const { input, options, isMulti, isReadOnly, selectValChanged, ...otherProps } = this.props;
    const self = this;
    function onChange(val) {
      val = isMulti ? val : val.slice(-1);
      if (isFunction(self.props.input?.onChange)) {
        self.props.input.onChange(val);
      }
      if (selectValChanged) {
        selectValChanged(val);
      }
    }

    const customStyles = {
      option: (provided, state) => ({
        ...provided,
        padding: 10
      }),
      control: (provided) => ({
        // none of react-select's styles are passed to <Control />

        ...provided,
        width: 'auto',
        borderColor: '#dedee0',
        borderRadius: 8,
        fontSize: '14px',
        minHeight: 42
      }),
      placeholder: (provided) => ({
        ...provided,
        color: '#999999'
      }),
      container: (provided) => ({
        ...provided,
        zIndex: 2
      }),
      dropdownIndicator: (provided) => ({
        ...provided,
        cursor: 'pointer'
      }),
      clearIndicator: (provided) => ({
        ...provided,
        cursor: 'pointer'
      }),
      singleValue: (provided, state) => {
        const opacity = state.isDisabled ? 0.5 : 1;
        const transition = 'opacity 300ms';

        return { ...provided, opacity, transition };
      }
    };

    return (
      <Select
        className="Select"
        styles={customStyles}
        {...input}
        options={options}
        isDisabled={isReadOnly}
        isMulti={true}
        onBlur={() => {}}
        onChange={onChange}
        {...otherProps}
      />
    );
  }
}

export default class YBMultiSelectWithLabel extends Component {
  render() {
    const { label, meta, ...otherProps } = this.props;
    return (
      <YBLabel label={label} meta={meta}>
        <YBNewMultiSelect {...otherProps} />
      </YBLabel>
    );
  }
}

// TODO: Rename all prior YBMultiSelect references to YBMultiSelectWithLabel.
export const YBMultiSelect = YBMultiSelectWithLabel;
