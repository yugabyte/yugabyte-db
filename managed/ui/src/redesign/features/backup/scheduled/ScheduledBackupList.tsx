/*
 * Created on Wed Jul 17 2024
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { FC } from 'react';
import { useToggle } from 'react-use';
import CreateScheduledBackupModal from './create/CreateScheduledBackupModal';
import { ScheduledBackupEmpty } from './ScheduledBackupEmpty';
import { AllowedTasks } from '../../../helpers/dtos';

interface ScheduledBackupListProps {
  universeUUID: string;
  allowedTasks: AllowedTasks;
}

const ScheduledBackupList: FC<ScheduledBackupListProps> = () => {
  const [createScheduledBackupModalVisible, toggleCreateScheduledBackupModalVisible] = useToggle(
    false
  );

  return (
    <div>
      <ScheduledBackupEmpty
        hasPerm={true}
        onActionButtonClick={() => {
          toggleCreateScheduledBackupModalVisible(true);
        }}
      />
      <CreateScheduledBackupModal
        visible={createScheduledBackupModalVisible}
        onHide={() => {
          toggleCreateScheduledBackupModalVisible(false);
        }}
      />
    </div>
  );
};

export default ScheduledBackupList;
