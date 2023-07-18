import { createContext, useReducer } from 'react';
import './MetricsComparisonModal.scss';
import moment from 'moment';

// We can define different filter types here, the type parameter should be
// valid type that moment supports except for custom and divider.
// if the filter type has a divider, we would just add a divider in the dropdown
// and custom filter would show custom date picker
export const filterTypes = [
  { label: 'Last 1 hr', type: 'hours', value: '1' },
  { label: 'Last 6 hrs', type: 'hours', value: '6' },
  { label: 'Last 12 hrs', type: 'hours', value: '12' },
  { label: 'Last 24 hrs', type: 'hours', value: '24' },
  { label: 'Last 7 days', type: 'days', value: '7' },
  { type: 'divider' },
  { label: 'Custom', type: 'custom' }
];

export const intervalTypes = [
  { label: 'Off', selectedLabel: 'Off', value: 'off' },
  { label: 'Every 1 minute', selectedLabel: '1 minute', value: 60000 },
  { label: 'Every 2 minutes', selectedLabel: '2 minute', value: 120000 }
];

export const DEFAULT_FILTER_KEY = 0;
export const DEFAULT_INTERVAL_KEY = 0;

export const FilterContext = createContext({});

const initialState = {
  filters: {
    startMoment: moment().subtract(
      filterTypes[DEFAULT_FILTER_KEY].value,
      filterTypes[DEFAULT_FILTER_KEY].type
    ),
    endMoment: moment(),
    nodePrefix: null,
    label: filterTypes[DEFAULT_FILTER_KEY].label,
    type: filterTypes[DEFAULT_FILTER_KEY].type,
    value: filterTypes[DEFAULT_FILTER_KEY].value
  },
  selectedMetrics: ['ysql_sql_latency', 'ysql_server_rpc_per_second'],
  nodeNameFirst: 'all',
  nodeNameSecond: 'all',
  refreshFilters: {
    refreshInterval: intervalTypes[DEFAULT_INTERVAL_KEY].value,
    refreshIntervalLabel: intervalTypes[DEFAULT_INTERVAL_KEY].selectedLabel
  },
  visibleMetricsSelectorModal: false,
  metricsData: {
    firstMetricsData: {},
    secondMetricsData: {}
  }
};

const reducer = (state, action) => {
  switch (action.type) {
    case 'CHANGE_GRAPH_FILTER':
      return {
        ...state,
        filters: { ...state.filters, ...action.payload }
      };
    case 'CHANGE_REFRESH_INTERVAL':
      return {
        ...state,
        refreshFilters: { ...action.payload }
      };
    case 'RESET_REFRESH_INTERVAL':
      return {
        ...state,
        refreshFilters: {
          refreshInterval: intervalTypes[DEFAULT_INTERVAL_KEY].value,
          refreshIntervalLabel: intervalTypes[DEFAULT_INTERVAL_KEY].selectedLabel
        }
      };
    case 'HIDE_METRICS_MODAL':
      return {
        ...state,
        visibleMetricsSelectorModal: false
      };
    case 'SHOW_METRICS_MODAL':
      return {
        ...state,
        visibleMetricsSelectorModal: true
      };
    case 'CHANGE_FIRST_NODE':
      return {
        ...state,
        nodeNameFirst: action.payload
      };
    case 'CHANGE_SECOND_NODE':
      return {
        ...state,
        nodeNameSecond: action.payload
      };
    case 'CHANGE_METRICS_DATA':
      return {
        ...state,
        metricsData: {
          ...state.metricsData,
          ...action.payload
        }
      };
    case 'CHANGE_SELECTED_METRICS':
      return {
        ...state,
        selectedMetrics: action.payload
      };
    default:
      throw new Error();
  }
};

export const ComparisonFilterContextProvider = ({ selectedUniverse, children }) => {
  initialState.filters.nodePrefix =
    selectedUniverse === 'all' ? selectedUniverse : selectedUniverse.universeDetails.nodePrefix;

  // If the universe has more than 1 node, set the selected node to the first and second nodes of the universe
  if (selectedUniverse?.universeDetails?.nodeDetailsSet?.length >= 2) {
    initialState.nodeNameFirst = selectedUniverse.universeDetails.nodeDetailsSet[0].nodeName;
    initialState.nodeNameSecond = selectedUniverse.universeDetails.nodeDetailsSet[1].nodeName;
  }

  const [state, dispatch] = useReducer(reducer, initialState);
  return <FilterContext.Provider value={[state, dispatch]}>{children}</FilterContext.Provider>;
};
