// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import Toggle from 'react-toggle';
import { isValidObject } from '../../../../utils/ObjectUtils';
import { YBLabel, DescriptionItem } from 'components/common/descriptors';
import 'react-toggle/style.css';
import './stylesheets/YBToggle.scss';

export default class YBToggle extends Component {

  render() {
    const { input, label, onToggle, isReadOnly, meta, insetError,
      subLabel, infoContent, infoTitle } = this.props;
    const onChange = function(event) {
      input.onChange(event);
      if (isValidObject(onToggle)) {
        onToggle(event);
      }
    };

    return (
      <YBLabel label={label} meta={meta} insetError={insetError}
               infoContent={infoContent} infoTitle={infoTitle}>
        <DescriptionItem title={subLabel}>
          <Toggle checked={!!input.value}  className="yb-toggle"
                  onChange={onChange} disabled={isReadOnly} />
        </DescriptionItem>
      </YBLabel>
    );
  }
}
