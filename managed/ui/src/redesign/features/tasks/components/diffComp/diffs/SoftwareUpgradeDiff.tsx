/*
 * Created on Fri May 17 2024
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { Task } from '../../../dtos';
import { TaskDiffBanner } from '../DiffBanners';
import DiffCard from '../DiffCard';
import { DiffCardWrapper } from '../DiffCardWrapper';
import { DiffComponentProps, DiffOperation } from '../dtos';
import { BaseDiff } from './BaseDiff';

/**
 * Represents a component for displaying the differences the task made during the software upgrade operation.
 * Extends the BaseDiff component.
 */
export class SoftwareUpgradeDiff extends BaseDiff<DiffComponentProps, {}> {
  task: Task;
  constructor(props: DiffComponentProps) {
    super(props);
    this.task = props.task;
  }

  getModalTitle() {
    return 'Software Upgrade';
  }

  getDiffComponent(): React.ReactElement {
    return (
      <DiffCardWrapper>
        <TaskDiffBanner task={this.task} diffCount={1} />
        <DiffCard
          attribute={{
            title: 'Software Version'
          }}
          operation={DiffOperation.CHANGED}
          beforeValue={{
            title: this.task.details.versionNumbers?.ybPrevSoftwareVersion as string
          }}
          afterValue={{
            title: this.task.details.versionNumbers?.ybSoftwareVersion as string
          }}
        />
        ;
      </DiffCardWrapper>
    );
  }
}

export default SoftwareUpgradeDiff;
