// Copyright (c) YugabyteDB, Inc.
import { Component, lazy, Suspense } from 'react';
import { YBLoadingCircleIcon } from '../components/common/indicators';
import { TaskDetailDrawer } from '../redesign/features/tasks';

const UniverseDetailContainer = lazy(() =>
  import('../components/universes/UniverseDetail/UniverseDetailContainer')
);

class UniverseDetail extends Component {
  render() {
    return (
      <div>
        <Suspense fallback={YBLoadingCircleIcon}>
          <TaskDetailDrawer />
          <UniverseDetailContainer uuid={this.props.params.uuid} {...this.props} />
        </Suspense>
      </div>
    );
  }
}
export default UniverseDetail;
