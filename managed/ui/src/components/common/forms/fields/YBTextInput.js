// Copyright (c) YugaByte, Inc.

import { Component } from 'react';
import { FormControl } from 'react-bootstrap';
import { isFunction } from 'lodash';
import { YBLabel } from '../../../../components/common/descriptors';
import { isDefinedNotNull, trimString } from '../../../../utils/ObjectUtils';

// TODO: Make default export after checking all corresponding imports.
export class YBTextInput extends Component {
  static defaultProps = {
    isReadOnly: false
  };

  componentDidMount() {
    const { initValue } = this.props;
    if (isDefinedNotNull(initValue)) this.props.input.onChange(initValue);
  }

  render() {
    const self = this;
    const {
      input,
      type,
      className,
      placeHolder,
      onValueChanged,
      isReadOnly,
      normalizeOnBlur,
      autocomplete = 'on'
    } = this.props;

    function onChange(event) {
      if (isFunction(onValueChanged)) {
        onValueChanged(event.target.value);
      }
      if (isDefinedNotNull(self.props.input) && isFunction(self.props.input.onChange)) {
        self.props.input.onChange(event.target.value);
      }
    }

    function onBlur(event) {
      if (isDefinedNotNull(self.props.input) && isFunction(self.props.input.onBlur)) {
        const trimmedValue = trimString(event.target.value);
        event.target.value = trimmedValue;
        if (isFunction(normalizeOnBlur)) {
          self.props.input.onBlur(normalizeOnBlur(trimmedValue));
        } else {
          self.props.input.onBlur(trimmedValue);
        }
      }
    }

    return (
      <FormControl
        {...input}
        placeholder={placeHolder}
        type={type}
        className={className}
        onChange={onChange}
        readOnly={isReadOnly}
        onBlur={onBlur}
        autocomplete={autocomplete}
      />
    );
  }
}

export default class YBTextInputWithLabel extends Component {
  render() {
    const {
      label,
      meta,
      insetError,
      infoContent,
      infoTitle,
      infoPlacement,
      containerClassName,
      ...otherProps
    } = this.props;
    return (
      <YBLabel
        label={label}
        meta={meta}
        insetError={insetError}
        infoContent={infoContent}
        infoTitle={infoTitle}
        infoPlacement={infoPlacement}
        classOverrides={containerClassName}
      >
        <YBTextInput {...otherProps} />
      </YBLabel>
    );
  }
}

export class YBControlledTextInput extends Component {
  render() {
    const {
      label,
      meta,
      input,
      type,
      className,
      placeHolder,
      onValueChanged,
      isReadOnly,
      val,
      infoContent,
      infoTitle,
      infoPlacement
    } = this.props;
    return (
      <YBLabel
        label={label}
        meta={meta}
        infoContent={infoContent}
        infoTitle={infoTitle}
        infoPlacement={infoPlacement}
      >
        <FormControl
          {...input}
          placeholder={placeHolder}
          type={type}
          className={className}
          onChange={onValueChanged}
          readOnly={isReadOnly}
          value={val}
        />
      </YBLabel>
    );
  }
}

// TODO: Deprecated. Rename all YBInputField references to YBTextInputWithLabel.
export const YBInputField = YBTextInputWithLabel;
