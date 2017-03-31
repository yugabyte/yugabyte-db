// Copyright (c) YugaByte, Inc.

import React, { Component, PropTypes } from 'react';

export default class YBCodeBlock extends Component {
  static propTypes = {
    label: PropTypes.string
  }

  render() {
    const { label } = this.props;

    return (
      <div className="yb-code-block">
        {label &&
          <label>
            {label}
          </label>
        }
        <pre>
          <code>{this.props.children}</code>
        </pre>
      </div>
    )
  }
}
