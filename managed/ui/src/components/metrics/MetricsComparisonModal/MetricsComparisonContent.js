import React, {useCallback, useContext, useEffect, useRef} from 'react';
import { FilterContext } from './ComparisonFilterContextProvider';
import { useQuery } from 'react-query';
import { getQueryMetrics } from '../../../actions/graph';
import { MetricsComparisonGraphPanel } from './MetricsComparisonGraphPanel';
import moment from 'moment';

export const MetricsComparisonContent = ({ universe, visible }) => {
  const [state, dispatch] = useContext(FilterContext);
  const { filters, nodeNameFirst, nodeNameSecond, selectedMetrics, refreshFilters } = state;

  const intervalRef = useRef();

  const stableDispatch = useCallback(dispatch, []);

  const refreshModalGraphQuery = useCallback(() => {
    const newFilter = {
      startMoment: moment().subtract(filters.value, filters.type),
      endMoment: moment()
    };
    stableDispatch({
      type: 'CHANGE_GRAPH_FILTER',
      payload: { ...newFilter }
    });
  }, [filters, stableDispatch]);

  useEffect(() => {
    if (visible) {
      refreshModalGraphQuery();
    } else {
      dispatch({
        type: 'RESET_REFRESH_INTERVAL'
      });
      clearInterval(intervalRef.current);
    }
  }, [visible, dispatch, refreshModalGraphQuery]);

  useEffect(() => {
    if (refreshFilters.refreshInterval !== 'off') {
      const id = setInterval(() => {
        refreshModalGraphQuery();
      }, refreshFilters.refreshInterval);
      intervalRef.current = id;
    }
    return () => {
      clearInterval(intervalRef.current);
    }
  }, [refreshFilters, refreshModalGraphQuery]);

  const queryParams = {};
  queryParams.start = filters.startMoment.unix();
  queryParams.end = filters.endMoment.unix();
  queryParams.nodePrefix = universe.universeDetails.nodePrefix;
  if (nodeNameFirst !== 'all') {
    queryParams.nodeName = nodeNameFirst;
  }
  queryParams.metrics = selectedMetrics;
  queryParams.isRecharts = true;

  useQuery(['getQueryMetrics', queryParams], () => getQueryMetrics(queryParams), {
    onSuccess: (response) => {
      dispatch({
        type: 'CHANGE_METRICS_DATA',
        payload: { firstMetricsData: response }
      });
    }
  });

  if (nodeNameSecond !== 'all') {
    queryParams.nodeName = nodeNameSecond;
  } else {
    delete queryParams.nodeName;
  }

  useQuery(['getQueryMetrics', queryParams], () => getQueryMetrics(queryParams), {
    onSuccess: (response) => {
      dispatch({
        type: 'CHANGE_METRICS_DATA',
        payload: { secondMetricsData: response }
      });
    }
  });

  const graphPanels = selectedMetrics.map((metricsKey) => {
    return <MetricsComparisonGraphPanel key={metricsKey} metricsKey={metricsKey} />;
  });

  return <>{graphPanels}</>;
};
