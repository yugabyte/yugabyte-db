// Copyright (c) YugabyteDB, Inc.

import { Component } from 'react';
import { FormattedNumber } from 'react-intl';
import { isFinite } from 'lodash';

export default class YBFormattedNumber extends Component {
  render() {
    if (!isFinite(this.props.value)) {
      return <span>n/a</span>;
    }
    return <FormattedNumber {...this.props} style={this.props.formattedNumberStyle} />;
  }
}
