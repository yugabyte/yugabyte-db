// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import Toggle from 'react-toggle';
import { isValidObject } from '../../../../utils/ObjectUtils';
import { YBLabel, DescriptionItem } from 'components/common/descriptors';
import 'react-toggle/style.css';

export default class YBToggle extends Component {

  render() {
    const { input, label, defaultChecked, onToggle, isReadOnly, meta, insetError, subLabel } = this.props;
    const onChange = function(event) {
      input.onChange(event);
      if (isValidObject(onToggle)) {
        onToggle(event);
      }
    };
    return (
      <YBLabel label={label} meta={meta} insetError={insetError}>
        <DescriptionItem title={subLabel}>
          <Toggle defaultChecked={defaultChecked} onChange={onChange} disabled={isReadOnly} />
        </DescriptionItem>
      </YBLabel>
    );
  }
}
