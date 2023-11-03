// Copyright (c) YugaByte, Inc.

import { Component, Suspense, lazy } from 'react';
import { YBLoadingCircleIcon } from '../components/common/indicators';

const HelpItemComponent = lazy(() => import('../components/help/HelpItem/HelpItemContainer'));

export default class Help extends Component {
  render() {
    return (
      <Suspense fallback={YBLoadingCircleIcon}>
        <div>
          <HelpItemComponent />
        </div>
      </Suspense>
    );
  }
}
