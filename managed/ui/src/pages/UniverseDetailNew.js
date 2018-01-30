// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';

import { UniverseDetailContainerNew } from '../components/universes';
import Universes from './Universes';

class UniverseDetailNew extends Component {
  render() {
    return (
      <Universes>
        <UniverseDetailContainerNew uuid={this.props.params.uuid}/>
      </Universes>
    );
  }
}
export default UniverseDetailNew;
