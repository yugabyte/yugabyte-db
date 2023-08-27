// Copyright (c) YugaByte, Inc.

import { Component } from 'react';

import { UniverseDetailContainer } from '../components/universes';

class UniverseDetail extends Component {
  render() {
    return (
      <div>
        <UniverseDetailContainer uuid={this.props.params.uuid} {...this.props} />
      </div>
    );
  }
}
export default UniverseDetail;
