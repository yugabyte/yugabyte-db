/*
 * Created on Fri May 17 2024
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { MutableRefObject } from 'react';
import clsx from 'clsx';
import { differenceWith, intersectionWith, isEqual } from 'lodash';
import { useTranslation } from 'react-i18next';
import { makeStyles } from '@material-ui/core';
import { ClusterType } from '../../../../../helpers/dtos';
import {
  Cluster,
  PlacementAZ,
  PlacementRegion
} from '../../../../universe/universe-form/utils/dto';
import { getPrimaryCluster } from '../../../../universe/universe-form/utils/helpers';
import { Task } from '../../../dtos';
import { DiffActions } from '../DiffActions';
import { DiffTitleBanner, TaskDiffBanner } from '../DiffBanners';
import DiffCard, { DiffCardRef } from '../DiffCard';
import { DiffCardWrapper } from '../DiffCardWrapper';
import { isAZEqual } from '../DiffUtils';
import { DiffComponentProps, DiffOperation, DiffProps } from '../dtos';
import { BaseDiff } from './BaseDiff';

/**
 * Represents a component for displaying the differences the task made during the edit operation.
 * Extends the BaseDiff component.
 */
export class UniverseDiff extends BaseDiff<DiffComponentProps, {}> {
  diffProps: DiffProps;
  task: Task;
  // Cards for the primary and async clusters.
  cards: Record<string, React.ReactElement<typeof DiffCard>[]>;

  // Refs for the diff cards. used to expand all cards.
  cardRefs: MutableRefObject<DiffCardRef>[];

  constructor(props: DiffComponentProps) {
    super(props);
    this.diffProps = props;
    this.task = props.task;
    this.cards = {
      [ClusterType.PRIMARY]: [],
      [ClusterType.ASYNC]: []
    };
    this.cardRefs = [];
  }

  getModalTitle() {
    return 'Universe';
  }

  // Get the differences in the instance type of the cluster.
  getInstanceTypeDiff(beforeValue: Cluster, afterValue: Cluster, clusterType: ClusterType) {
    // If the instance type is changed, add a diff card.
    if (beforeValue.userIntent.instanceType !== afterValue.userIntent.instanceType) {
      this.cards[clusterType].push(
        <DiffCard
          ref={(ref) => ref && this.cardRefs?.push({ current: ref })}
          attribute={{ title: 'Instance Type' }}
          operation={DiffOperation.CHANGED}
          beforeValue={{
            title: beforeValue.userIntent.instanceType!
          }}
          afterValue={{
            title: afterValue.userIntent.instanceType!
          }}
        />
      );
    }
  }

  // Get the differences in the cloud list of the cluster.
  // Flow: getCloudListDiffComponent -> getRegionsDiffComponent -> getPlacementAZDiffComponent
  // cloudlist will call getRegionsDiffComponent to get the differences in the regions.
  // getRegionsDiffComponent will call getPlacementAZDiffComponent to get the differences in the placement AZs.
  // getPlacementAZDiffComponent will return the diff component for the placement AZs.

  getCloudListDiffComponent(beforeValue: Cluster, afterValue: Cluster, clusterType: ClusterType) {
    const beforeCloudList = beforeValue.placementInfo?.cloudList ?? [];
    const afterCloudList = afterValue.placementInfo?.cloudList ?? [];

    // Get the cloud list that are removed and added.
    const cloudListRemoved = differenceWith(
      beforeCloudList,
      afterCloudList,
      (a, b) => a.code === b.code
    );
    const cloudListAdded = differenceWith(
      afterCloudList,
      beforeCloudList,
      (a, b) => a.code === b.code
    );

    // Create diff cards for the removed and added cloud lists.
    cloudListRemoved.forEach((cloudList) => {
      cloudList.regionList.forEach((region) => {
        this.cards[clusterType].push(this.getRegionsDiffComponent(region, undefined));
      });
    });

    cloudListAdded.forEach((cloudList) => {
      cloudList.regionList.forEach((region) => {
        this.cards[clusterType].push(this.getRegionsDiffComponent(undefined, region));
      });
    });

    // Get the cloud list that are changed.
    const cloudListChanged = intersectionWith(
      beforeCloudList,
      afterCloudList,
      (a, b) => a.code === b.code && !isEqual(a, b)
    );

    // Create diff cards for the changed cloud lists.
    cloudListChanged.forEach((cloudList) => {
      const afterCloudVal = afterCloudList.find((r) => r.code === cloudList.code);

      // Get the regions that are removed and added.
      const regionAdded = differenceWith(
        cloudList.regionList,
        afterCloudVal!.regionList,
        (a, b) => a.code === b.code
      );
      const regionRemoved = differenceWith(
        afterCloudVal!.regionList,
        cloudList.regionList,
        (a, b) => a.code === b.code
      );

      // Create diff cards for the removed and added regions.
      regionAdded.forEach((region) => {
        this.cards[clusterType].push(this.getRegionsDiffComponent(undefined, region));
      });

      regionRemoved.forEach((region) => {
        this.cards[clusterType].push(this.getRegionsDiffComponent(region, undefined));
      });

      // Get the regions that are changed.
      const regionChanged = intersectionWith(
        cloudList.regionList,
        afterCloudVal!.regionList,
        (a, b) => a.code === b.code && !isEqual(a, b)
      );

      regionChanged.forEach((region) => {
        const afterRegion = afterCloudVal!.regionList.find((r) => r.code === region.code);
        this.cards[clusterType].push(this.getRegionsDiffComponent(region, afterRegion));
      });
    });
  }

  // Get the diff component for the regions.
  getRegionsDiffComponent(
    beforeRegion?: PlacementRegion,
    afterRegion?: PlacementRegion
  ): React.ReactElement {
    // If both regions are null, throw an error.
    if (!beforeRegion && !afterRegion) {
      throw new Error('Both before and after regions are null');
    }

    const atttributeCompList = [<span>Region</span>];
    const beforeCompList = [<div>{beforeRegion ? beforeRegion.name : '---'}</div>];
    const afterCompList = [<div>{afterRegion ? afterRegion.name : '---'}</div>];

    // If the region is added, add a diff card.
    if (!beforeRegion && afterRegion) {
      afterRegion.azList.forEach((az, index) => {
        atttributeCompList.push(<RegionAttributePlaceholder key={index} />);
        const [beforePlacementComp, afterPlacementComp] = this.getPlacementAZDiffComponent(
          undefined,
          az
        );

        beforeCompList.push(beforePlacementComp);
        afterCompList.push(afterPlacementComp);
      });
    }

    // If the region is removed, add a diff card.
    if (beforeRegion && !afterRegion) {
      beforeRegion!.azList.forEach((az, index) => {
        atttributeCompList.push(<RegionAttributePlaceholder key={index} />);
        const [beforePlacementComp, afterPlacementComp] = this.getPlacementAZDiffComponent(
          az,
          undefined
        );

        beforeCompList.push(beforePlacementComp);
        afterCompList.push(afterPlacementComp);
      });
    }

    // If the region is changed, add a diff card.
    if (beforeRegion && afterRegion) {
      const azListAdded = differenceWith(
        afterRegion.azList,
        beforeRegion.azList,
        (a, b) => a.name === b.name && !isAZEqual(a, b)
      );
      const azListRemoved = differenceWith(
        beforeRegion.azList,
        afterRegion.azList,
        (a, b) => a.name === b.name && !isAZEqual(a, b)
      );

      // Create diff cards for the added and removed placement AZs.
      azListAdded.forEach((az, index) => {
        atttributeCompList.push(<RegionAttributePlaceholder key={index} />);
        const [beforePlacementComp, afterPlacementComp] = this.getPlacementAZDiffComponent(
          undefined,
          az
        );

        beforeCompList.push(beforePlacementComp);
        afterCompList.push(afterPlacementComp);
      });

      azListRemoved.forEach((az, index) => {
        atttributeCompList.push(<RegionAttributePlaceholder key={index} />);
        const [beforePlacementComp, afterPlacementComp] = this.getPlacementAZDiffComponent(
          az,
          undefined
        );

        beforeCompList.push(beforePlacementComp);
        afterCompList.push(afterPlacementComp);
      });

      // Get the placement AZs that are changed.
      const azListChanged = intersectionWith(
        beforeRegion.azList,
        afterRegion.azList,
        // Check if the AZs are equal based on specific fields.
        (a, b) => a.name === b.name && !isAZEqual(a, b)
      );

      // Create diff cards for the changed placement AZs.
      azListChanged.forEach((az, index) => {
        const afterAz = afterRegion.azList.find((a) => a.name === az.name);
        const [beforePlacementComp, afterPlacementComp] = this.getPlacementAZDiffComponent(
          az,
          afterAz
        );

        atttributeCompList.push(<RegionAttributePlaceholder key={index} />);
        beforeCompList.push(beforePlacementComp);
        afterCompList.push(afterPlacementComp);
      });
    }

    return (
      <DiffCard
        operation={
          !beforeRegion
            ? DiffOperation.ADDED
            : !afterRegion
            ? DiffOperation.REMOVED
            : DiffOperation.CHANGED
        }
        ref={(ref) => ref && this.cardRefs?.push({ current: ref })}
        attribute={{
          title: 'Region',
          element: <>{atttributeCompList}</>
        }}
        afterValue={{
          title: afterRegion?.name ?? '---',
          element: <>{afterCompList}</>
        }}
        beforeValue={{
          title: beforeRegion?.name ?? '---',
          element: <>{beforeCompList}</>
        }}
      />
    );
  }

  // Get the diff component for the placement AZs.
  getPlacementAZDiffComponent(
    beforePlacementAZ?: PlacementAZ,
    afterPlacementAZ?: PlacementAZ
  ): React.ReactElement[] {
    if (!beforePlacementAZ && !afterPlacementAZ) {
      throw new Error('Both before and after placement AZs are null');
    }

    return [
      <PlacementAzComponent
        key={beforePlacementAZ?.uuid}
        placementAz={beforePlacementAZ!}
        operation={DiffOperation.REMOVED}
        // If the placement AZ is added(new), display the empty values.
        displayEmpty={!beforePlacementAZ}
      />,
      <PlacementAzComponent
        key={afterPlacementAZ?.uuid}
        placementAz={afterPlacementAZ!}
        operation={DiffOperation.ADDED}
        // If the placement AZ is removed(existing), display the empty values.
        displayEmpty={!afterPlacementAZ}
      />
    ];
  }

  getDiffComponent(): React.ReactElement {
    // Get the primary cluster before and after the edit operation.
    const beforePrimaryCluster = getPrimaryCluster(this.diffProps.beforeData);
    const afterPrimaryCluster = getPrimaryCluster(this.diffProps.afterData);

    // Get the differences in the instance type of the primary cluster.
    this.getInstanceTypeDiff(beforePrimaryCluster!, afterPrimaryCluster!, ClusterType.PRIMARY);
    // Get the differences in the cloud list of the primary cluster.
    this.getCloudListDiffComponent(
      beforePrimaryCluster!,
      afterPrimaryCluster!,
      ClusterType.PRIMARY
    );

    return (
      <DiffCardWrapper>
        <DiffActions
          onExpandAll={() => {
            this.cardRefs.forEach((ref) => {
              ref?.current?.onExpand(true);
            });
          }}
          changesCount={
            this.cards[ClusterType.PRIMARY].length + this.cards[ClusterType.ASYNC].length
          }
        />
        <TaskDiffBanner
          task={this.task}
          diffCount={this.cards[ClusterType.PRIMARY].length + this.cards[ClusterType.ASYNC].length}
        />
        <DiffTitleBanner title="Primary Cluster" />
        {this.cards[ClusterType.PRIMARY]}
      </DiffCardWrapper>
    );
  }
}

const useStyles = makeStyles((theme) => ({
  root: {
    '& ul': {
      listStyleType: "'↳'",
      paddingLeft: '18px',
      marginBottom: 0,
      '& > li': {
        paddingLeft: '4px'
      }
    },
    color: theme.palette.grey[900],
    lineHeight: '32px',
    fontSize: '13px'
  },
  region: {
    paddingLeft: '6px !important',
    marginBottom: 0
  }
}));

// Placeholder for the region attribute.
const RegionAttributePlaceholder = () => {
  const classes = useStyles();
  const { t } = useTranslation('translation', { keyPrefix: 'taskDetails.diffModal.universeDiff' });

  return (
    <div className={classes.root}>
      <ul>
        <li>{t('availabilityZone')}</li>
        <ul>
          <li>{t('nodes')}</li>
          <li>{t('preferred')}</li>
        </ul>
      </ul>
    </div>
  );
};

const useStylePlacementAzComponent = makeStyles((theme) => ({
  root: {
    color: theme.palette.grey[900],
    lineHeight: '32px',
    fontSize: '13px',
    '& > span': {
      width: 'fit-content'
    }
  },
  strikeOutRed: {
    '& > span': {
      backgroundColor: '#FEEDED',
      mixBlendMode: 'darken'
    }
  },
  strikeOutGreen: {
    '& > span': {
      backgroundColor: '#CDEFE1',
      mixBlendMode: 'darken'
    }
  }
}));

// Component for displaying the placement AZ.
const PlacementAzComponent = ({
  placementAz,
  displayEmpty,
  operation
}: {
  placementAz: PlacementAZ;
  operation: DiffOperation;
  displayEmpty?: boolean;
}) => {
  const classes = useStylePlacementAzComponent();

  return (
    <div
      className={clsx(
        classes.root,
        !displayEmpty
          ? operation === DiffOperation.ADDED
            ? classes.strikeOutGreen
            : classes.strikeOutRed
          : ''
      )}
    >
      <span>{displayEmpty ? '---' : placementAz.name}</span>
      <br />
      <span>{displayEmpty ? '---' : placementAz.numNodesInAZ}</span>
      <br />
      <span>{displayEmpty ? '---' : placementAz.isAffinitized + ''}</span>
      <br />
    </div>
  );
};

export default UniverseDiff;
