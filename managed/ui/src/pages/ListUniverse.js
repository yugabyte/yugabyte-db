// Copyright (c) YugaByte, Inc.

import React, {Component} from 'react';

import { ListUniverseContainer } from '../containers/universes';
import Universes from './Universes';

export default class ListUniverse extends Component {
  render() {
    return (
      <Universes>
        <ListUniverseContainer />
      </Universes>
    )
  }
}
