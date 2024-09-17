/*
 * Created on Tue Sep 03 2024
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { forwardRef, useImperativeHandle, useRef } from 'react';
import { Page, PageRef } from './models/RestoreContext';
import PrefetchData from './pages/Prefetch/PrefetchData';
import RenameKeyspace from './pages/RenameKeyspace/RenameKeyspace';
import RestoreFinal from './pages/RestoreFinal/RestoreFinal';
import RestoreSource from './pages/RestoreSource/RestoreSource';
import RestoreTarget from './pages/RestoreTarget/RestoreTarget';
import { GetRestoreContext } from './RestoreUtils';

const SwitchRestorePages = forwardRef((_props, forwardRef) => {
  const [{ formProps }] = GetRestoreContext();

  const currentComponentRef = useRef<PageRef>(null);

  useImperativeHandle(forwardRef, () => currentComponentRef.current, [
    currentComponentRef.current,
    formProps.currentPage
  ]);

  const getCurrentComponent = () => {
    switch (formProps.currentPage) {
      case Page.PREFETCH_DATA:
        return <PrefetchData ref={currentComponentRef} />;
      case Page.SOURCE:
        return <RestoreSource ref={currentComponentRef} />;
      case Page.TARGET:
        return <RestoreTarget ref={currentComponentRef} />;
      case Page.RENAME_KEYSPACES:
        return <RenameKeyspace ref={currentComponentRef} />;
      case Page.RESTORE_FINAL:
        return <RestoreFinal ref={currentComponentRef} />;
      default:
        return null;
    }
  };

  return getCurrentComponent();
});

SwitchRestorePages.displayName = 'SwitchRestorePages';

export default SwitchRestorePages;
