/*
 * Created on Fri May 17 2024
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { keys } from 'lodash';
import { DiffActions } from '../DiffActions';
import { DiffTitleBanner, TaskDiffBanner } from '../DiffBanners';
import { DiffCardWrapper } from '../DiffCardWrapper';
import DiffCard, { DiffCardRef } from '../DiffCard';
import { Task } from '../../../dtos';
import { BaseDiff } from './BaseDiff';
import { getDiffsInObject } from '../DiffUtils';
import { DiffComponentProps, DiffOperation, DiffProps } from '../dtos';

/**
 * Represents a component for displaying the differences the task made during the GFlag operation.
 * Extends the BaseDiff component.
 */
export class GFlagsDiff extends BaseDiff<DiffComponentProps, {}> {
  diffProps: DiffProps;
  cardRefs: React.RefObject<DiffCardRef>[];
  task: Task;

  constructor(props: DiffComponentProps) {
    super(props);
    this.diffProps = props;
    this.cardRefs = [];
    this.task = props.task;
  }

  getModalTitle() {
    return 'GFlags';
  }

  getDiffComponent(): React.ReactElement {
    const { beforeData, afterData } = this.diffProps;

    const cards: Record<string, React.ReactElement<typeof DiffCard>[]> = {
      masterGFlags: [],
      tserverGFlags: []
    };

    // Get the differences in the master and tserver GFlags.
    const masterGFlagsDiffs = getDiffsInObject(
      beforeData.clusters[0].userIntent.specificGFlags!.perProcessFlags.value!.MASTER!,
      afterData.clusters[0].userIntent.specificGFlags!.perProcessFlags.value!.MASTER!
    );

    const tserverGFlagsDiffs = getDiffsInObject(
      beforeData.clusters[0].userIntent.specificGFlags!.perProcessFlags.value!.TSERVER!,
      afterData.clusters[0].userIntent.specificGFlags!.perProcessFlags.value!.TSERVER!
    );

    // Create the diff cards for the master and tserver GFlags.
    keys(masterGFlagsDiffs).forEach((key) => {
      masterGFlagsDiffs[key].forEach((diff: DiffProps) => {
        cards.masterGFlags.push(
          <DiffCard
            ref={(ref) => this.cardRefs?.push({ current: ref })}
            attribute={{
              title: diff.attribute ?? ''
            }}
            beforeValue={{
              title: (diff.beforeData as unknown) as string
            }}
            afterValue={{
              title: (diff.afterData as unknown) as string
            }}
            operation={diff.operation as DiffOperation}
          />
        );
      });
    });

    keys(tserverGFlagsDiffs).forEach((key) => {
      tserverGFlagsDiffs[key].forEach((diff: DiffProps) => {
        cards.tserverGFlags.push(
          <DiffCard
            ref={(ref) => this.cardRefs.push({ current: ref })}
            attribute={{
              title: diff.attribute ?? ''
            }}
            beforeValue={{
              title: (diff.beforeData as unknown) as string
            }}
            afterValue={{
              title: (diff.afterData as unknown) as string
            }}
            operation={diff.operation as DiffOperation}
          />
        );
      });
    });
    return (
      <DiffCardWrapper>
        <DiffActions
          onExpandAll={() => {
            // Expand all the cards.
            this.cardRefs.forEach((ref) => {
              ref?.current?.onExpand(true);
            });
          }}
          // Get the count of changes.
          changesCount={cards.masterGFlags.length + cards.tserverGFlags.length}
        />
        <TaskDiffBanner
          task={this.task}
          diffCount={cards.masterGFlags.length + cards.tserverGFlags.length}
        />
        {cards.tserverGFlags.length > 0 && <DiffTitleBanner title="T-SERVER FLAG" />}
        {/* Render the diff cards for the tserver GFlags. */}
        {cards.tserverGFlags}
        {cards.masterGFlags.length > 0 && (
          <DiffTitleBanner title="MASTER FLAG" showLegends={false} />
        )}
        {/* Render the diff cards for the master GFlags. */}
        {cards.masterGFlags}
      </DiffCardWrapper>
    );
  }
}

export default GFlagsDiff;
