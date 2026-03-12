/*
 * Created on Fri May 17 2024
 *
 * Copyright 2021 YugabyteDB, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import React, { MutableRefObject } from 'react';
import clsx from 'clsx';
import { differenceWith, intersectionWith, isEqual, keys, size } from 'lodash';
import { useTranslation } from 'react-i18next';
import { Box, makeStyles, Typography } from '@material-ui/core';

import { getPrimaryCluster } from '../../../../universe/universe-form/utils/helpers';
import { DiffActions } from '../DiffActions';
import { DiffTitleBanner, TaskDiffBanner } from '../DiffBanners';
import DiffCard, { DiffCardRef } from '../DiffCard';
import { DiffCardWrapper } from '../DiffCardWrapper';
import { FieldOperations, getFieldOpertions, isAZEqual } from '../DiffUtils';
import { BaseDiff } from './BaseDiff';

import { Task } from '../../../dtos';
import { DiffComponentProps, DiffOperation, DiffProps } from '../dtos';
import {
  AZOverridePerAZ,
  Cluster,
  CloudType,
  ClusterType,
  MasterPlacementMode,
  PlacementAZ,
  PlacementRegion,
  UniverseDetails,
  DeviceInfo,
  UserIntent
} from '../../../../universe/universe-form/utils/dto';

const DeviceInfoFields: Record<keyof Omit<DeviceInfo, 'mountPoints' | 'cloudVolumeEncryption'>, string> = {
  volumeSize: 'Volume Size',
  numVolumes: 'Number of Volumes',
  diskIops: 'Disk IOPS',
  throughput: 'Throughput',
  storageClass: 'Storage Class',
  storageType: 'Storage Type'
};

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

  // keep track of changes made by the edit operation
  changesCount: number;

  constructor(props: DiffComponentProps) {
    super(props);
    this.diffProps = props;
    this.task = props.task;
    this.cards = {
      [ClusterType.PRIMARY]: [],
      [ClusterType.ASYNC]: []
    };
    this.cardRefs = [];
    this.changesCount = 0;
  }

  getModalTitle() {
    return 'Universe';
  }

  // Get the differences in the instance type of the cluster.
  getInstanceTypeDiff(beforeValue: Cluster, afterValue: Cluster, clusterType: ClusterType) {
    // If the instance type is changed, add a diff card.
    if (beforeValue?.userIntent?.instanceType !== afterValue?.userIntent?.instanceType) {
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
      this.changesCount++;
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
        afterCloudVal!.regionList,
        cloudList.regionList,
        (a, b) => a.code === b.code
      );
      const regionRemoved = differenceWith(
        cloudList.regionList,
        afterCloudVal!.regionList,
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
        (a, b) => a.name === b.name
      );
      const azListRemoved = differenceWith(
        beforeRegion.azList,
        afterRegion.azList,
        (a, b) => a.name === b.name
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
    const fieldOperations = getFieldOpertions(beforePlacementAZ, afterPlacementAZ!);
    const afterFieldOperations = getFieldOpertions(beforePlacementAZ, afterPlacementAZ!, true);

    // Count the number of changes made by the edit operation.
    keys(fieldOperations).forEach((key) => {
      if (
        fieldOperations[key] !== DiffOperation.UNCHANGED ||
        afterFieldOperations[key] !== DiffOperation.UNCHANGED
      ) {
        this.changesCount++;
      }
    });

    return [
      <PlacementAzComponent
        key={beforePlacementAZ?.uuid}
        placementAz={beforePlacementAZ!}
        operation={
          beforePlacementAZ
            ? afterPlacementAZ
              ? DiffOperation.CHANGED
              : DiffOperation.REMOVED
            : DiffOperation.ADDED
        }
        // If the placement AZ is added(new), display the empty values.
        displayEmpty={!beforePlacementAZ}
        fieldOperations={fieldOperations}
      />,
      <PlacementAzComponent
        key={afterPlacementAZ?.uuid}
        placementAz={afterPlacementAZ!}
        operation={DiffOperation.ADDED}
        // If the placement AZ is removed(existing), display the empty values.
        displayEmpty={!afterPlacementAZ}
        fieldOperations={afterFieldOperations}
      />
    ];
  }

  getRegionListDiff = (placementInfo: Cluster['placementInfo']) => {
    return placementInfo?.cloudList.map((cloud) => {
      return cloud.regionList.map((region) => {
        return (
          <Box display="flex" key={region.code} mt={1} flexDirection="column">
            <Typography variant="h5">{region.code}</Typography>
            <Box pl={2}>
              {region.azList.map((az) => (
                <Typography key={az.name} variant="body2">
                  {az.name} - {az.numNodesInAZ} node
                </Typography>
              ))}
            </Box>
          </Box>
        );
      });
    });
  };

  isMasterPlacementPresent(beforeValue: Cluster, afterValue: Cluster) {
    return (
      beforeValue?.userIntent?.dedicatedNodes === true ||
      afterValue?.userIntent?.dedicatedNodes === true
    );
  }

  getMasterPlacementDiffComponent(
    beforeValue: Cluster,
    afterValue: Cluster,
    clusterType: ClusterType
  ) {
    const attributes: JSX.Element[] = [];
    const beforeComp: JSX.Element[] = [];
    const afterComp: JSX.Element[] = [];

    const { attributes: attr1, beforeComp: before1, afterComp: after1 } = this.getDeviceInfoFields(
      beforeValue.userIntent.masterDeviceInfo,
      afterValue.userIntent.masterDeviceInfo
    );
    if (attr1.length !== 0) {
      attributes.push(<DeviceInfoHeader content="Master">{attr1}</DeviceInfoHeader>);
      beforeComp.push(<DeviceInfoField content={<>&nbsp;</>} />, ...before1);
      afterComp.push(<DeviceInfoField content={<>&nbsp;</>} />, ...after1);
    }

    if (beforeValue?.userIntent?.dedicatedNodes !== afterValue?.userIntent?.dedicatedNodes) {
      attributes.push(<DeviceInfoHeader content="Master Placement" />);
      beforeComp.push(
        <DeviceInfoField
          content={
            beforeValue?.userIntent?.dedicatedNodes
              ? MasterPlacementMode.DEDICATED
              : MasterPlacementMode.COLOCATED
          }
          operation={DiffOperation.REMOVED}
        />
      );
      afterComp.push(
        <DeviceInfoField
          content={
            afterValue?.userIntent?.dedicatedNodes
              ? MasterPlacementMode.DEDICATED
              : MasterPlacementMode.COLOCATED
          }
          operation={DiffOperation.ADDED}
        />
      );
      this.changesCount++;
    }

    if (beforeValue.userIntent.masterInstanceType !== afterValue.userIntent.masterInstanceType) {
      this.cards[clusterType].push(
        <DiffCard
          ref={(ref) => ref && this.cardRefs?.push({ current: ref })}
          attribute={{ title: 'Master Instance Type' }}
          operation={DiffOperation.CHANGED}
          beforeValue={{
            title: beforeValue.userIntent.masterInstanceType!
          }}
          afterValue={{
            title: afterValue.userIntent.masterInstanceType!
          }}
        />
      );
      this.changesCount++;
    }

    const { attributes: attr2, beforeComp: before2, afterComp: after2 } = this.getDeviceInfoFields(
      beforeValue.userIntent.deviceInfo,
      afterValue.userIntent.deviceInfo
    );

    if (attr2.length !== 0) {
      attributes.push(<DeviceInfoHeader content="T-Server">{attr2}</DeviceInfoHeader>);
      beforeComp.push(<DeviceInfoField content={<>&nbsp;</>} />, ...before2);
      afterComp.push(<DeviceInfoField content={<>&nbsp;</>} />, ...after2);
    }

    if (!isEqual(beforeValue.placementInfo, afterValue.placementInfo)) {
      const afterElem = this.getRegionListDiff(afterValue.placementInfo);
      const beforeElem = this.getRegionListDiff(beforeValue.placementInfo);

      if (afterElem || beforeElem) {
        attributes.push(<DeviceInfoHeader content="Placement" />);
        afterComp.push(...(afterElem?.flat() ?? []));
        beforeComp.push(...(beforeElem?.flat() ?? []));
      }
    }

    if (attributes.length === 0) {
      return;
    }

    this.cards[clusterType].push(
      <DiffCard
        operation={DiffOperation.CHANGED}
        ref={(ref) => ref && this.cardRefs?.push({ current: ref })}
        attribute={{
          title: 'Master Placement',
          element: <>{attributes}</>
        }}
        afterValue={{
          title: `${afterValue?.userIntent?.dedicatedNodes
              ? MasterPlacementMode.DEDICATED
              : MasterPlacementMode.COLOCATED
            }`,
          element: <>{afterComp}</>
        }}
        beforeValue={{
          title: `${beforeValue?.userIntent?.dedicatedNodes
              ? MasterPlacementMode.DEDICATED
              : MasterPlacementMode.COLOCATED
            }`,
          element: <>{beforeComp}</>
        }}
      />
    );
  }

  getDeviceInfoFields = (
    beforeDeviceInfo?: DeviceInfo | null,
    afterDeviceInfo?: DeviceInfo | null
  ) => {
    const attributes: JSX.Element[] = [];
    const beforeComp: JSX.Element[] = [];
    const afterComp: JSX.Element[] = [];

    if (!beforeDeviceInfo && !afterDeviceInfo) {
      return { attributes, beforeComp, afterComp };
    }

    if (!beforeDeviceInfo) {
      beforeDeviceInfo = {} as DeviceInfo;
    }
    if (!afterDeviceInfo) {
      afterDeviceInfo = {} as DeviceInfo;
    }

    (keys(DeviceInfoFields) as (keyof typeof DeviceInfoFields)[]).forEach((field) => {
      if (beforeDeviceInfo![field] !== afterDeviceInfo![field]) {
        const label = DeviceInfoFields[field];
        attributes.push(<DeviceInfoField content={label} CustomElem="li" />);
        beforeComp.push(
          <DeviceInfoField
            content={beforeDeviceInfo![field]?.toString() ?? '---'}
            operation={DiffOperation.REMOVED}
          />
        );
        afterComp.push(
          <DeviceInfoField
            content={afterDeviceInfo![field]?.toString() ?? '---'}
            operation={DiffOperation.ADDED}
          />
        );
        this.changesCount++;
      }
    });

    return { attributes, beforeComp, afterComp };
  };

  getDeviceInfoDiffComponent(beforeValue: Cluster, afterValue: Cluster, clusterType: ClusterType) {
    const beforeDeviceInfo = beforeValue.userIntent?.deviceInfo ?? null;
    const afterDeviceInfo = afterValue.userIntent?.deviceInfo ?? null;

    if (!beforeDeviceInfo || !afterDeviceInfo) {
      return;
    }

    const { attributes, beforeComp, afterComp } = this.getDeviceInfoFields(
      beforeDeviceInfo,
      afterDeviceInfo
    );

    if (attributes.length === 0) {
      return;
    }

    this.cards[clusterType].push(
      <DiffCard
        ref={(ref) => ref && this.cardRefs?.push({ current: ref })}
        attribute={{
          element: <DeviceInfoHeader content="">{attributes}</DeviceInfoHeader>,
          title: 'Instance Configuration'
        }}
        operation={DiffOperation.CHANGED}
        beforeValue={{ element: <>{beforeComp}</> }}
        afterValue={{ element: <>{afterComp}</> }}
      />
    );
  }

  // Build a map of placement UUID -> AZ name from cluster placementInfo.
  getPlacementUuidToAzName(cluster: Cluster): Record<string, string> {
    const map: Record<string, string> = {};
    cluster?.placementInfo?.cloudList?.forEach((cloud) => {
      cloud?.regionList?.forEach((region: PlacementRegion) => {
        region?.azList?.forEach((az: PlacementAZ) => {
          if (az?.uuid) map[az.uuid] = az.name ?? az.uuid;
        });
      });
    });
    return map;
  }

  getK8sAzOverridesDiffComponent(
    beforeValue: Cluster,
    afterValue: Cluster,
    clusterType: ClusterType
  ) {
    const beforeProviderK8s = beforeValue?.userIntent?.providerType === CloudType.kubernetes;
    const afterProviderK8s = afterValue?.userIntent?.providerType === CloudType.kubernetes;
    if (!beforeProviderK8s && !afterProviderK8s) return;

    const beforeOverrides: Record<string, AZOverridePerAZ> =
      beforeValue?.userIntent?.userIntentOverrides?.azOverrides ?? {};
    const afterOverrides: Record<string, AZOverridePerAZ> =
      afterValue?.userIntent?.userIntentOverrides?.azOverrides ?? {};
    if (isEqual(beforeOverrides, afterOverrides)) return;

    const beforeAzNames = this.getPlacementUuidToAzName(beforeValue);
    const afterAzNames = this.getPlacementUuidToAzName(afterValue);
    const allAzUuids = Array.from(
      new Set([...Object.keys(beforeOverrides), ...Object.keys(afterOverrides)])
    ).sort();

    const k8sOverrideFields = ['volumeSize', 'numVolumes', 'storageClass'] as const;
    const fieldLabels: Record<string, string> = {
      volumeSize: 'Volume Size',
      numVolumes: 'Number of Volumes',
      storageClass: 'Storage Class'
    };
    const processTypes = ['TSERVER', 'MASTER'] as const;
    const processLabels: Record<string, string> = { TSERVER: 'T-Server', MASTER: 'Master' };

    const isEmptyVal = (v: unknown): boolean =>
      v === undefined || v === null || (typeof v === 'string' && v.trim() === '');
    const toDisplayVal = (v: unknown, fallback: string): string =>
      isEmptyVal(v) ? fallback : String(v);

    type K8sChange = {
      processType: 'TSERVER' | 'MASTER';
      field: (typeof k8sOverrideFields)[number];
      beforeVal: string;
      afterVal: string;
      beforeOp?: DiffOperation;
      afterOp?: DiffOperation;
    };
    type K8sFlatRow = {
      label: string;
      indent: number;
      beforeVal: string;
      afterVal: string;
      beforeOp?: DiffOperation;
      afterOp?: DiffOperation;
    };
    const flatRows: K8sFlatRow[] = [];

    allAzUuids.forEach((azUuid) => {
      const beforeEntry = beforeOverrides[azUuid];
      const afterEntry = afterOverrides[azUuid];
      const azName = afterAzNames[azUuid] ?? beforeAzNames[azUuid] ?? azUuid;
      const beforeHasAz = !!beforeEntry;
      const afterHasAz = !!afterEntry;
      const changes: K8sChange[] = [];

      processTypes.forEach((processType) => {
        const beforeDevice = beforeEntry?.perProcess?.[processType]?.deviceInfo;
        const afterDevice = afterEntry?.perProcess?.[processType]?.deviceInfo;

        k8sOverrideFields.forEach((field) => {
          const rawBefore = beforeHasAz ? beforeDevice?.[field] : undefined;
          const rawAfter = afterHasAz ? afterDevice?.[field] : undefined;
          const beforeVal = toDisplayVal(rawBefore, '---');
          const afterVal = toDisplayVal(rawAfter, '---');
          const same = isEmptyVal(rawBefore) && isEmptyVal(rawAfter) ? true : beforeVal === afterVal;
          if (same) return;

          this.changesCount++;
          const beforeOp = !beforeHasAz ? undefined : !afterHasAz ? DiffOperation.REMOVED : !same ? DiffOperation.REMOVED : undefined;
          const afterOp = !afterHasAz ? undefined : !beforeHasAz ? DiffOperation.ADDED : !same ? DiffOperation.ADDED : undefined;
          changes.push({ processType, field, beforeVal, afterVal, beforeOp, afterOp });
        });
      });

      if (changes.length > 0) {
        flatRows.push({ label: azName, indent: 0, beforeVal: '', afterVal: '' });
        const byProcess = new Map<string, K8sChange[]>();
        changes.forEach((c) => {
          if (!byProcess.has(c.processType)) byProcess.set(c.processType, []);
          byProcess.get(c.processType)!.push(c);
        });
        processTypes.forEach((processType) => {
          const list = byProcess.get(processType);
          if (!list?.length) return;
          flatRows.push({ label: processLabels[processType], indent: 1, beforeVal: '', afterVal: '' });
          list.forEach((c) => {
            flatRows.push({
              label: fieldLabels[c.field] ?? c.field,
              indent: 2,
              beforeVal: c.beforeVal,
              afterVal: c.afterVal,
              beforeOp: c.beforeOp,
              afterOp: c.afterOp
            });
          });
        });
      }
    });

    if (flatRows.length === 0) return;

    this.cards[clusterType].push(
      <DiffCard
        ref={(ref) => ref && this.cardRefs?.push({ current: ref })}
        attribute={{
          title: 'K8s AZ Overrides',
          element: <K8sAZOverrideRows flatRows={flatRows} />
        }}
        operation={DiffOperation.CHANGED}
        beforeValue={{ title: '', element: <K8sAZOverrideRows flatRows={flatRows} kind="before" /> }}
        afterValue={{ title: '', element: <K8sAZOverrideRows flatRows={flatRows} kind="after" /> }}
      />
    );
  }

  getCommunicationDiffComponent(
    beforeValue: UniverseDetails,
    afterValue: UniverseDetails,
    clusterType: ClusterType
  ) {
    const beforeCommunicationPort = beforeValue.communicationPorts;
    const afterCommunicationPort = afterValue.communicationPorts;
    if (!beforeCommunicationPort || !afterCommunicationPort) {
      return;
    }

    const attributes: JSX.Element[] = [];
    const beforeComp: JSX.Element[] = [];
    const afterComp: JSX.Element[] = [];

    (Object.keys(beforeCommunicationPort) as (keyof typeof beforeCommunicationPort)[]).forEach(
      (key) => {
        if (beforeCommunicationPort[key] !== afterCommunicationPort[key]) {
          attributes.push(<DeviceInfoField content={key} CustomElem="li" />);
          beforeComp.push(
            <DeviceInfoField
              content={beforeCommunicationPort[key]?.toString() ?? '---'}
              operation={DiffOperation.REMOVED}
            />
          );
          afterComp.push(
            <DeviceInfoField
              content={afterCommunicationPort[key]?.toString() ?? '---'}
              operation={DiffOperation.ADDED}
            />
          );
          this.changesCount++;
        }
      }
    );
    if (attributes.length === 0) {
      return;
    }
    this.cards[clusterType].push(
      <DiffCard
        ref={(ref) => ref && this.cardRefs?.push({ current: ref })}
        attribute={{
          element: <DeviceInfoHeader content="Communication Ports">{attributes}</DeviceInfoHeader>,
          title: 'Communication Ports'
        }}
        operation={DiffOperation.CHANGED}
        beforeValue={{
          element: (
            <>
              <DeviceInfoField content={<>&nbsp;</>} />
              {beforeComp}
            </>
          )
        }}
        afterValue={{
          element: (
            <>
              <DeviceInfoField content={<>&nbsp;</>} />
              {afterComp}
            </>
          )
        }}
      />
    );
  }

  getInstanceTagsDiffComponent(
    beforeInstanceTags: UserIntent['instanceTags'],
    afterInstanceTags: UserIntent['instanceTags']
  ) {
    if (
      (!beforeInstanceTags && !afterInstanceTags) ||
      isEqual(beforeInstanceTags, afterInstanceTags)
    ) {
      // If both instance tags are null or equal, return.
      return;
    }
    const attributes: JSX.Element[] = [];
    const beforeComp: JSX.Element[] = [];
    const afterComp: JSX.Element[] = [];

    const changesCountByOperation = {
      [DiffOperation.ADDED]: 0,
      [DiffOperation.REMOVED]: 0,
      [DiffOperation.CHANGED]: 0
    };
    // Check for the added instance tags.
    beforeInstanceTags &&
      Object.keys(beforeInstanceTags).forEach((key) => {
        if (!afterInstanceTags || !afterInstanceTags[key]) {
          attributes.push(<InstanceTagsField value={key} />);
          beforeComp.push(
            <InstanceTagsField operation={DiffOperation.ADDED} value={beforeInstanceTags[key]} />
          );
          afterComp.push(<InstanceTagsField operation={DiffOperation.REMOVED} value={'---'} />);
          this.changesCount++;
          changesCountByOperation[DiffOperation.REMOVED]++;
        } else if (afterInstanceTags && beforeInstanceTags[key] !== afterInstanceTags[key]) {
          attributes.push(<InstanceTagsField value={key} />);
          beforeComp.push(
            <InstanceTagsField operation={DiffOperation.REMOVED} value={beforeInstanceTags[key]} />
          );
          afterComp.push(
            <InstanceTagsField operation={DiffOperation.ADDED} value={afterInstanceTags[key]} />
          );
          this.changesCount++;
          changesCountByOperation[DiffOperation.CHANGED]++;
        }
      });
    afterInstanceTags &&
      Object.keys(afterInstanceTags).forEach((key) => {
        if (!beforeInstanceTags || !beforeInstanceTags[key]) {
          attributes.push(<InstanceTagsField value={key} />);
          beforeComp.push(<InstanceTagsField operation={DiffOperation.REMOVED} value={'---'} />);
          afterComp.push(
            <InstanceTagsField operation={DiffOperation.ADDED} value={afterInstanceTags[key]} />
          );
          this.changesCount++;
          changesCountByOperation[DiffOperation.ADDED]++;
        }
      });
    this.cards[ClusterType.PRIMARY].push(
      <DiffCard
        ref={(ref) => ref && this.cardRefs?.push({ current: ref })}
        attribute={{
          title: 'Instance Tags',
          element: <>{attributes}</>
        }}
        afterValue={{
          title: '',
          element: <>{afterComp}</>
        }}
        beforeValue={{
          title: '',
          element: <>{beforeComp}</>
        }}
        operation={
          changesCountByOperation[DiffOperation.CHANGED] > 0
            ? DiffOperation.CHANGED
            : changesCountByOperation[DiffOperation.ADDED] > 0
              ? DiffOperation.ADDED
              : DiffOperation.REMOVED
        }
      />
    );
  }

  getDiffComponent(): React.ReactElement {
    this.changesCount = 0;
    // Get the primary cluster before and after the edit operation.
    const beforePrimaryCluster = getPrimaryCluster(this.diffProps.beforeData as UniverseDetails);
    const afterPrimaryCluster = getPrimaryCluster(this.diffProps.afterData as UniverseDetails);

    if (!beforePrimaryCluster || !afterPrimaryCluster) {
      return <Typography variant="h6">Changes not found</Typography>;
    }
    // Get the differences in the instance type of the primary cluster.
    this.getInstanceTypeDiff(beforePrimaryCluster!, afterPrimaryCluster!, ClusterType.PRIMARY);

    // Get the differences in the cloud list of the primary cluster.
    this.getCloudListDiffComponent(
      beforePrimaryCluster!,
      afterPrimaryCluster!,
      ClusterType.PRIMARY
    );

    if (this.isMasterPlacementPresent(beforePrimaryCluster!, afterPrimaryCluster!)) {
      this.getMasterPlacementDiffComponent(
        beforePrimaryCluster!,
        afterPrimaryCluster!,
        ClusterType.PRIMARY
      );
    } else {
      // Get the differences in the device info of the primary cluster.
      this.getDeviceInfoDiffComponent(
        beforePrimaryCluster!,
        afterPrimaryCluster!,
        ClusterType.PRIMARY
      );
    }

    this.getCommunicationDiffComponent(
      this.diffProps.beforeData as UniverseDetails,
      this.diffProps.afterData as UniverseDetails,
      ClusterType.PRIMARY
    );

    this.getK8sAzOverridesDiffComponent(
      beforePrimaryCluster,
      afterPrimaryCluster,
      ClusterType.PRIMARY
    );

    this.getInstanceTagsDiffComponent(
      beforePrimaryCluster.userIntent.instanceTags,
      afterPrimaryCluster.userIntent.instanceTags
    );

    return (
      <DiffCardWrapper>
        <DiffActions
          onExpandAll={(flag) => {
            this.cardRefs.forEach((ref) => {
              ref?.current?.onExpand(flag);
            });
          }}
          changesCount={this.changesCount}
        />
        <TaskDiffBanner task={this.task} diffCount={this.changesCount} />
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

// Single-line rows for K8s AZ Overrides so attribute, before, and after align on the same line.
const useK8sRowStyles = makeStyles((theme) => ({
  row: {
    minHeight: 32,
    lineHeight: '32px',
    fontSize: 13,
    color: theme.palette.grey[900],
    display: 'block',
    '&:empty': { minHeight: 32 }
  },
  marker: {
    '&::before': {
      content: '"↳ "',
    }
  }
}));

const K8sAZOverrideRows = ({
  flatRows,
  kind
}: {
  flatRows: Array<{
    label: string;
    indent: number;
    beforeVal: string;
    afterVal: string;
    beforeOp?: DiffOperation;
    afterOp?: DiffOperation;
  }>;
  kind?: 'before' | 'after';
}) => {
  const classes = useK8sRowStyles();
  const indentPx = 16;

  if (kind === 'before') {
    return (
      <>
        {flatRows.map((row, i) => (
          <div key={i} className={classes.row}>
            <DeviceInfoField content={row.beforeVal || ''} operation={row.beforeOp} />
          </div>
        ))}
      </>
    );
  }
  if (kind === 'after') {
    return (
      <>
        {flatRows.map((row, i) => (
          <div key={i} className={classes.row}>
            <DeviceInfoField content={row.afterVal || ''} operation={row.afterOp} />
          </div>
        ))}
      </>
    );
  }
  return (
    <>
      {flatRows.map((row, i) => (
        <div key={i} className={clsx(classes.row, row.indent > 0 && classes.marker)} style={{ paddingLeft: row.indent * indentPx }}>
          {row.label}
        </div>
      ))}
    </>
  );
};

const useStylePlacementAzComponent = makeStyles((theme) => ({
  root: {
    color: theme.palette.grey[900],
    lineHeight: '32px',
    fontSize: '13px',
    '& > span': {
      width: 'fit-content'
    },
    '& > .Changed': {
      backgroundColor: '#FDE5E5',
      mixBlendMode: 'darken',
      textDecoration: 'line-through'
    },
    '& > .Added': {
      backgroundColor: '#CDEFE1',
      mixBlendMode: 'darken'
    },
    '& > .Removed': {
      backgroundColor: '#FEEDED',
      mixBlendMode: 'darken',
      textDecoration: 'line-through'
    }
  }
}));

// Component for displaying the placement AZ.

const PlacementAzComponent = ({
  placementAz,
  displayEmpty,
  fieldOperations
}: {
  placementAz: PlacementAZ;
  operation: DiffOperation;
  displayEmpty?: boolean;
  fieldOperations?: FieldOperations;
}) => {
  const classes = useStylePlacementAzComponent();

  return (
    <div className={clsx(classes.root)}>
      <span className={clsx(!displayEmpty && fieldOperations?.name)}>
        {displayEmpty ? '---' : placementAz.name}
      </span>
      <br />
      <span className={clsx(!displayEmpty && fieldOperations?.numNodesInAZ)}>
        {displayEmpty ? '---' : placementAz.numNodesInAZ}
      </span>
      <br />
      <span className={clsx(!displayEmpty && fieldOperations?.isAffinitized)}>
        {displayEmpty ? '---' : placementAz.isAffinitized + ''}
      </span>
      <br />
    </div>
  );
};

const InstanceTagsField = ({
  value,
  operation
}: {
  value?: string;
  operation?: DiffOperation;
}): JSX.Element => {
  const classes = useStylePlacementAzComponent();
  return (
    <div className={classes.root} title={value}>
      <span className={operation}>
        {(value ?? '').length > 20 ? `${value?.substring(0, 20)}...` : value}
      </span>
    </div>
  );
};

const DeviceInfoField = ({
  content,
  operation,
  CustomElem = 'div'
}: {
  content: string | React.ReactElement;
  operation?: DiffOperation;
  CustomElem?: keyof JSX.IntrinsicElements;
}): JSX.Element => {
  const classes = useStylePlacementAzComponent();
  return (
    <CustomElem className={classes.root}>
      <span className={operation}>{content}</span>
    </CustomElem>
  );
};

const DeviceInfoHeader = ({
  content,
  children
}: {
  content: string;
  children?: React.ReactElement[] | React.ReactElement;
}): JSX.Element => {
  const classes = useStyles();
  return (
    <div className={classes.root}>
      <b>{content}</b>
      <ul>{children}</ul>
    </div>
  );
};

export default UniverseDiff;
