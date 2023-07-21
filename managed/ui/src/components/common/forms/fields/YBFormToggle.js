// Copyright (c) YugaByte, Inc.

import { Component } from 'react';
import Toggle from 'react-toggle';
import { YBLabel, DescriptionItem } from '../../../../components/common/descriptors';
import 'react-toggle/style.css';
import './stylesheets/YBToggle.scss';
import { isFunction } from 'lodash';
import { isDefinedNotNull } from '../../../../utils/ObjectUtils';

export default class YBFormToggle extends Component {
  handleOnChange = (event) => {
    const { field, onChange } = this.props;

    if (isDefinedNotNull(field) && isFunction(field.onChange)) {
      field.onChange(event);
    }

    if (isFunction(onChange)) {
      onChange(this.props, event);
    }
  };

  render() {
    const {
      label,
      isReadOnly,
      meta,
      insetError,
      subLabel,
      infoContent,
      infoTitle,
      field
    } = this.props;
    return (
      <YBLabel
        label={label}
        meta={meta}
        insetError={insetError}
        infoContent={infoContent}
        infoTitle={infoTitle}
      >
        <DescriptionItem title={subLabel}>
          <Toggle
            checked={field.value}
            name={field.name}
            className="yb-toggle"
            onChange={this.handleOnChange}
            disabled={isReadOnly}
          />
        </DescriptionItem>
      </YBLabel>
    );
  }
}
