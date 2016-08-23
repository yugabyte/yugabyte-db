// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import UniverseDetailContainer from '../containers/UniverseDetailContainer';

class UniverseDetail extends Component {
  render() {
    return (
      <UniverseDetailContainer uuid={this.props.params.uuid}/>
    );
  }
}
export default UniverseDetail;
