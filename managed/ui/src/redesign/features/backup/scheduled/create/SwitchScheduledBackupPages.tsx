/*
 * Created on Wed Jul 17 2024
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { forwardRef, useContext, useImperativeHandle, useRef } from 'react';

import GeneralSettings from './pages/GeneralSettings/GeneralSettings';
import BackupObjects from './pages/BackupObjects/BackupObjects';
import BackupFrequency from './pages/BackupFrequency/BackupFrequency';
import BackupSummary from './pages/BackupSummary/BackupSummary';

import {
  Page,
  PageRef,
  ScheduledBackupContext,
  ScheduledBackupContextMethods
} from './models/ScheduledBackupContext';

const SwitchScheduledBackupPages = forwardRef((_props, forwardRef) => {
  const [{ formProps }] = (useContext(
    ScheduledBackupContext
  ) as unknown) as ScheduledBackupContextMethods;

  const currentComponentRef = useRef<PageRef>(null);

  useImperativeHandle(forwardRef, () => currentComponentRef.current, [
    currentComponentRef.current,
    formProps.currentPage
  ]);

  const getCurrentComponent = () => {
    switch (formProps.currentPage) {
      case Page.GENERAL_SETTINGS:
        return <GeneralSettings ref={currentComponentRef} />;
      case Page.BACKUP_OBJECTS:
        return <BackupObjects ref={currentComponentRef} />;
      case Page.BACKUP_FREQUENCY:
        return <BackupFrequency ref={currentComponentRef} />;
      case Page.BACKUP_SUMMARY:
        return <BackupSummary ref={currentComponentRef} />;
      default:
        return null;
    }
  };

  return getCurrentComponent();
});

SwitchScheduledBackupPages.displayName = 'SwitchScheduledBackupPages';

export default SwitchScheduledBackupPages;
