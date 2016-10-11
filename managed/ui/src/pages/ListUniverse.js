import ListUniverseContainer from '../containers/ListUniverseContainer';
import React, {Component} from 'react';
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
