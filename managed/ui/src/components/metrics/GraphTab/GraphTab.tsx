// Copyright (c) YugaByte, Inc.

import React, { FC, Fragment, useEffect } from 'react';
import { useDispatch, useSelector } from 'react-redux';

import {
  queryMetrics,
  queryMetricsSuccess,
  queryMetricsFailure,
} from '../../../actions/graph';
import {
  getTabContent
} from '../../../utils/GraphUtils';
import {
  isNonEmptyArray,
  isNonEmptyString,
} from '../../../utils/ObjectUtils';
import {
  MetricsData,
  GraphFilter,
  MetricQueryParams
} from '../../../redesign/helpers/dtos';
import { MetricMeasure, MetricConsts } from '../../metrics/constants';

export const GraphTab: FC<MetricsData> = ({
  type,
  metricsKey,
  nodePrefixes,
  selectedUniverse,
  title,
  tableName
}) => {
  let tabContent = null;
  const insecureLoginToken = useSelector((state: any) => state.customer.INSECURE_apiToken);
  const { currentUser } = useSelector((state: any) => state.customer);
  const graph = useSelector((state: any) => state.graph);
  const {
    startMoment,
    endMoment,
    nodeName,
    nodePrefix,
    currentSelectedRegion,
    metricMeasure,
    outlierType,
    outlierNumNodes,
    selectedRegionClusterUUID,
    selectedRegionCode,
    selectedZoneName
  }: GraphFilter = graph.graphFilter;
  const dispatch: any = useDispatch();

  const queryMetricsType = () => {
    const splitTopNodes = (isNonEmptyString(nodeName) && nodeName === MetricConsts.TOP) ? 1 : 0;
    const metricsWithSettings = metricsKey.map((metricKey) => {
      const settings: any = {
        metric: metricKey,
      }
      if (metricMeasure === MetricMeasure.OUTLIER) {
        settings.nodeAggregation = "AVG";
        settings.splitMode = outlierType;
        settings.splitCount = outlierNumNodes;
        // TODO: Will fix this hardcoded value in PLAT-5636
        settings.splitType = "NODE";
      } else if (metricMeasure === MetricMeasure.OVERALL) {
        settings.splitTopNodes = splitTopNodes;
      }
      return settings;
    });

    const params: any = {
      metricsWithSettings: metricsWithSettings,
      start: startMoment.format('X'),
      end: endMoment.format('X'),
    };
    if (isNonEmptyString(nodePrefix) && nodePrefix !== MetricConsts.ALL) {
      params.nodePrefix = nodePrefix;
    }
    if (isNonEmptyString(nodeName) && nodeName !== MetricConsts.ALL && nodeName !== MetricConsts.TOP) {
      params.nodeNames = [nodeName];
    }
    // In case of universe metrics , nodePrefix comes from component itself
    if (isNonEmptyArray(nodePrefixes)) {
      params.nodePrefix = nodePrefixes[0];
    }
    // If specific region or cluster is selected from region dropdown, pass clusterUUID and region code
    if (isNonEmptyString(selectedRegionClusterUUID)) {
      params.clusterUuids = [selectedRegionClusterUUID];
    }

    if (isNonEmptyString(selectedZoneName)) {
      params.availabilityZones = [selectedZoneName];
    }

    if (isNonEmptyString(selectedRegionCode)) {
      params.regionCodes = [selectedRegionCode];
    }

    if (isNonEmptyString(tableName)) {
      params.tableName = tableName;
    }

    queryMetricsVaues(params, type);
  };

  const queryMetricsVaues = (params: MetricQueryParams, type: string) => {
    dispatch(queryMetrics(params)).then((response: any) => {
      if (!response.error) {
        dispatch(queryMetricsSuccess(response.payload, type));
      } else {
        dispatch(queryMetricsFailure(response.payload, type));
      }
    });
  }

  useEffect(() => {
    queryMetricsType();
  }, [nodeName, // eslint-disable-line react-hooks/exhaustive-deps
    nodePrefix,
    startMoment,
    endMoment,
    currentSelectedRegion,
    metricMeasure,
    outlierType,
    outlierNumNodes
  ]);

  tabContent = getTabContent(
    graph,
    selectedUniverse,
    type,
    metricsKey,
    title,
    currentUser,
    insecureLoginToken
  );

  return (
    <Fragment>
      {tabContent}
    </Fragment>
  );
}
