// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import PropTypes from 'prop-types';

export default class YBLabel extends Component {
  static propTypes = {
    insetError: PropTypes.bool, // true => inset error message inside text/textarea fields
  };

  render() {
    const { label, insetError, meta: { touched, error, invalid }, onLabelClick } = this.props;

    return (
      <div className={`form-group ${ touched && invalid ? 'has-error' : ''}`} onClick={onLabelClick}>
        {label &&
          <label className="form-item-label">
            {label}
          </label>
        }
        <div className="yb-field-group">
          {this.props.children}
          {touched && error &&
            <div className={`help-block ${insetError ? 'embed-error' : 'standard-error'}`}>
              <span>{error}</span>
            </div>
          }
        </div>
      </div>
    );
  }
}
