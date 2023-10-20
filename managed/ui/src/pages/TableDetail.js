// Copyright (c) YugaByte, Inc.

import { Component, lazy, Suspense } from 'react';
import Universes from './Universes';
import { YBLoadingCircleIcon } from '../components/common/indicators';

const TableDetailContainer = lazy(() =>
  import('../components/tables/TableDetail/TableDetailContainer')
);

export default class TableDetail extends Component {
  render() {
    return (
      <Universes>
        <Suspense fallback={YBLoadingCircleIcon}>
          <TableDetailContainer
            universeUUID={this.props.params.uuid}
            tableUUID={this.props.params.tableUUID}
          />
        </Suspense>
      </Universes>
    );
  }
}
