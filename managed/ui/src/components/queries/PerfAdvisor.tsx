import React, { useState, FC, ReactNode, useEffect, useCallback } from 'react';
import { useTranslation } from 'react-i18next';
import { useQuery, useQueryClient } from 'react-query';
import { useSelector } from 'react-redux';
import { Button } from 'react-bootstrap';
import { MenuItem } from '@material-ui/core';
import { RecommendationBox } from './RecommendationBox';
import { YBPanelItem } from '../panels';
import { YBLoading } from '../../components/common/indicators';
import { YBButton } from '../common/forms/fields';
import { performanceRecommendationApi, QUERY_KEY } from './helpers/api';
import { formatPerfRecommendationsData } from './helpers/utils';
import { EXTERNAL_LINKS } from './helpers/constants';
import { ybFormatDate } from '../../redesign/helpers/DateUtils';
import { isEmptyString, isNonEmptyString } from '../../utils/ObjectUtils';
import {
  IndexAndShardingRecommendationData,
  PerfRecommendationData,
  RecommendationType,
  SortDirection,
  LastRunData
} from '../../redesign/utils/dtos';
import { UniverseState, getUniverseStatus } from '../universes/helpers/universeHelpers';
import { YBSelect } from '../../redesign/components';
import { RbacValidator } from '../../redesign/features/rbac/common/RbacApiPermValidator';
import { ApiPermissionMap } from '../../redesign/features/rbac/ApiAndUserPermMapping';
import dbSettingsIcon from './images/db-settings.svg';
import documentationIcon from './images/documentation.svg';
import EmptyTrayIcon from './images/empty-tray.svg';
import WarningIcon from './images/warning.svg';

import './PerfAdvisor.scss';

interface RecommendationDetailProps {
  data: PerfRecommendationData | IndexAndShardingRecommendationData;
  key: string;
  isResolved?: boolean;
}

const LastRunStatus = {
  PENDING: 'PENDING',
  RUNNING: 'RUNNING',
  COMPLETED: 'COMPLETED',
  FAILED: 'FAILED'
};

const TranslationTypeMap = {
  ALL: 'All',
  RANGE_SHARDING: 'SchemaSuggestion',
  QUERY_LOAD_SKEW: 'QueryLoadSkew',
  CONNECTION_SKEW: 'ConnectionSkew',
  CPU_SKEW: 'CpuSkew',
  CPU_USAGE: 'CpuUsage',
  UNUSED_INDEX: 'IndexSuggestion',
  HOT_SHARD: 'HotShard',
  REJECTED_CONNECTIONS: 'RejectedConnections'
} as const;

/** Generates unique string based on recommendation type for the purpose of setting unique key on React component */
const generateUniqueId = (type: string, ...identifiers: string[]) => {
  return `${type.replaceAll(' ', '')}-${identifiers
    .map((x) => x.replaceAll(' ', ''))
    .join('-')}-${Math.random().toString(16).slice(2)}`;
};

// These are the only two recommendation types related to DB
const DATABASE_TYPE_SUGGESTIONS = [
  RecommendationType.UNUSED_INDEX,
  RecommendationType.RANGE_SHARDING
];

export const PerfAdvisor: FC = () => {
  // Initialize state variables
  const currentUserTimezone = useSelector((state: any) => state.customer.currentUser.data.timezone);
  const [isLoading, setIsLoading] = useState<Boolean>(true);
  const [lastScanTime, setLastScanTime] = useState('');
  const [isLastRunCompleted, setIsLastRunCompleted] = useState<boolean>(false);
  const [isLastRunNotFound, setIsLastRunNotFound] = useState<boolean>(false);
  const [recommendations, setRecommendations] = useState<RecommendationDetailProps[]>([]);
  const [databaseSelection, setDatabaseSelection] = useState('');
  const [databaseOptionList, setDatabaseOptionList] = useState<ReactNode[]>([]);
  const [suggestionType, setSuggestionType] = useState(RecommendationType.ALL);
  const [isPerfCallFail, setIsPerfCallFail] = useState<Boolean>(false);
  const [scanStatus, setScanStatus] = useState<string>('');
  const [errorMessage, setErrorMessage] = useState<string>('');

  // Initialize the query client and translation
  const queryClient = useQueryClient();
  const { t } = useTranslation();

  // Get universe UUID and check status of Universe
  const currentUniverse = useSelector((state: any) => state.universe.currentUniverse);
  const universeUUID = currentUniverse?.data?.universeUUID;
  const universeStatus = getUniverseStatus(currentUniverse?.data);
  const isUniversePaused = universeStatus.state === UniverseState.PAUSED;
  const isUniverseUpdating = universeStatus.state === UniverseState.PENDING;

  const { isFetched: isLastRunFetched, data: lastRunData, refetch: lastRunRefetch } = useQuery(
    QUERY_KEY.fetchPerfLastRun,
    () => performanceRecommendationApi.fetchPerfLastRun(universeUUID),
    {
      onSuccess: (data: LastRunData) => {
        setScanStatus(data?.state);
        // If the universe is in PENDING or RUNNING state, ensure we call the API
        // till it status moves to COMPLETED or FAILED
        if (data.state === LastRunStatus.PENDING || data.state === LastRunStatus.RUNNING) {
          setTimeout(() => {
            queryClient.invalidateQueries([QUERY_KEY.fetchPerfLastRun]);
          }, 5000);
          setIsLastRunCompleted(false);
        } else if (data.state === LastRunStatus.COMPLETED) {
          setIsLastRunCompleted(true);
        } else if (data.state === LastRunStatus.FAILED) {
          setIsLoading(false);
        }
        setIsLastRunNotFound(false);
        setLastScanTime(data?.endTime);
      },
      onError: (error: any) => {
        // If lastRun API returns 404, ensure to update state for lastRunNotFound
        // This will display the Scan Button page
        if (error.request.status === 404) {
          setIsLoading(false);
          setIsLastRunNotFound(true);
        }
      }
    }
  );

  const queryParams = {
    filter: {
      universeId: universeUUID,
      isStale: false
    },
    sortBy: 'recommendationTimestamp',
    direction: SortDirection.ASC,
    offset: 0,
    limit: 50
  };

  const { isError: isPerfRecommendationError, refetch: perfRecommendationsRefetch } = useQuery(
    QUERY_KEY.fetchPerfRecommendations,
    () => performanceRecommendationApi.fetchPerfRecommendationsList(queryParams),
    {
      enabled: isLastRunFetched && !isLastRunNotFound,
      onSuccess: (perfRecommendationsData) => {
        updatePerfRecommendations(perfRecommendationsData, lastRunData!, isPerfRecommendationError);
      },
      onError: () => {
        setIsLoading(false);
        setLastScanTime(lastRunData!.endTime);
        setIsPerfCallFail(true);
      }
    }
  );

  useEffect(() => {
    if (isLastRunCompleted) {
      const fetchData = async () => {
        const { data, isError } = await perfRecommendationsRefetch();
        updatePerfRecommendations(data, lastRunData!, isError);
      };
      fetchData();
    }
    // Whenever the lastRun status moves to COMPLETED state
    // we need to make a call to page API
  }, [isLastRunCompleted]); // eslint-disable-line react-hooks/exhaustive-deps

  const updatePerfRecommendations = (
    perfData: any,
    lastRunPerfData: LastRunData,
    isPerfError: boolean
  ) => {
    setIsPerfCallFail(isPerfError);
    scanStatus === LastRunStatus.RUNNING || scanStatus === LastRunStatus.PENDING
      ? setIsLoading(true)
      : setIsLoading(false);
    const perfRecommendations = formatPerfRecommendationsData(perfData);
    const newRecommendations = processRecommendationData(perfRecommendations);
    setRecommendations(newRecommendations);
    setLastScanTime(lastRunPerfData?.endTime);
    localStorage.setItem(
      universeUUID,
      JSON.stringify({
        data: newRecommendations,
        lastUpdated: lastRunPerfData?.endTime
      })
    );
  };

  const handleDbSelection = (event: any) => {
    setSuggestionType(RecommendationType.ALL);
    setDatabaseSelection(event.target.value);
  };

  const handleSuggestionTypeSelection = (e: any) => {
    const nextSuggestionType: RecommendationType =
      RecommendationType[(e.target.value as unknown) as RecommendationType];
    setSuggestionType(nextSuggestionType);
  };

  const processRecommendationData = useCallback(
    (data: PerfRecommendationData[] | IndexAndShardingRecommendationData[]) => {
      const recommendationArr: RecommendationDetailProps[] = [];
      const databasesInRecommendations: string[] = [];
      data?.forEach((recommendation) => {
        if (DATABASE_TYPE_SUGGESTIONS.includes(recommendation.type)) {
          const { target: databaseName } = recommendation;
          if (!databasesInRecommendations.includes(databaseName)) {
            databasesInRecommendations.push(databaseName);
          }
        }
        recommendationArr.push({
          data: recommendation,
          key: generateUniqueId(recommendation.type, recommendation.target)
        });
      });
      setDatabaseOptionList([
        <MenuItem value={''} key={'all-databases'}>
          {t('clusterDetail.performance.advisor.AllDatabases')}
        </MenuItem>,
        [...databasesInRecommendations].map((databaseName: string) => (
          <MenuItem value={databaseName} key={`ysql-${databaseName}`}>
            {databaseName}
          </MenuItem>
        ))
      ]);
      return recommendationArr;
    },
    [setDatabaseOptionList, t]
  );

  const checkDatabasesInRecommendations = useCallback(
    (recommendationsList: RecommendationDetailProps[]) => {
      const databasesInRecommendations: string[] = [];
      recommendationsList.forEach((recommendation) => {
        if (DATABASE_TYPE_SUGGESTIONS.includes(recommendation.data.type)) {
          const { target: databaseName } = recommendation.data;
          if (!databasesInRecommendations.includes(databaseName)) {
            databasesInRecommendations.push(databaseName);
          }
        }
      });
      setDatabaseOptionList([
        <MenuItem value={''} key={'all-databases'}>
          {t('clusterDetail.performance.advisor.AllDatabases')}
        </MenuItem>,
        [...databasesInRecommendations].map((databaseName: string) => (
          <MenuItem value={databaseName} key={`ysql-${databaseName}`}>
            {databaseName}
          </MenuItem>
        ))
      ]);
    },
    [t]
  );

  // handleScan is triggered when user clicks on Scan/ReScan
  const handleScan = async () => {
    setIsLoading(true);
    try {
      await performanceRecommendationApi.startPefRunManually(universeUUID);
      // If start_manually fails initially, we need to reset the error message between re-renders
      // to ensure we have no error message when it passes successfully
      setErrorMessage('');
    } catch (e: any) {
      setErrorMessage(e?.response?.data?.error);
      setIsLastRunCompleted(false);
    }
    await lastRunRefetch();
  };

  useEffect(() => {
    const checkForPreviousScanData = () => {
      const previousScanResultJSON = localStorage.getItem(universeUUID);
      const previousScanResult = previousScanResultJSON && JSON.parse(previousScanResultJSON);
      // Scanning time is based on browser session storage
      if (previousScanResult?.data && previousScanResult?.lastUpdated) {
        setRecommendations(previousScanResult?.data);
        checkDatabasesInRecommendations(previousScanResult?.data);
        if (previousScanResult?.lastUpdated) {
          setLastScanTime(previousScanResult?.lastUpdated);
        }
      }
    };
    void checkForPreviousScanData();
  }, [universeUUID]); // eslint-disable-line react-hooks/exhaustive-deps

  const handleResolve = (id: string, isResolved: boolean) => {
    const copyRecommendations = [...recommendations];
    const userSelectedRecommendation = copyRecommendations.find((rec) => rec.key === id);
    if (userSelectedRecommendation) {
      userSelectedRecommendation.isResolved = isResolved;
    }
    setRecommendations(copyRecommendations);
  };

  const filteredByDatabaseRecommendations = recommendations.filter((rec) => {
    if (databaseSelection) {
      const isMatchingDb =
        DATABASE_TYPE_SUGGESTIONS.includes(rec.data.type) && rec.data.target === databaseSelection;
      return isMatchingDb;
    }
    return true;
  });
  const displayedRecomendations = filteredByDatabaseRecommendations.filter((rec) => {
    if (suggestionType !== RecommendationType.ALL) {
      return rec.data.type === suggestionType;
    }
    return true;
  });

  const recommendationTypeList: Record<string, boolean> = {
    [RecommendationType.ALL.toString()]: true
  };
  filteredByDatabaseRecommendations.forEach((curr: RecommendationDetailProps) => {
    recommendationTypeList[curr.data.type] = true;
  });
  const recommendationTypes = Object.keys(recommendationTypeList);
  const recommendationLabel =
    displayedRecomendations?.length > 1 ? 'Recommendations' : 'Recommendation';

  if (isLoading) {
    return (
      <YBPanelItem
        header={
          <div className="perfAdvisor__containerTitleGrid">
            <YBLoading text="Scanning" />
          </div>
        }
      />
    );
  }

  return (
    // This dialog is shown is when the last run API fails with 404
    <div className="parentPerfAdvisor">
      <RbacValidator
        accessRequiredOn={{ ...ApiPermissionMap.GET_PERF_RECOMENDATION_BY_PAGE, onResource: universeUUID }}
      >
        {isLastRunNotFound && (!recommendations.length || isEmptyString(lastScanTime)) && (
          <YBPanelItem
            header={
              <div className="perfAdvisor">
                <div className="perfAdvisor__containerTitleGrid">
                  <div className="contentContainer">
                    <img src={dbSettingsIcon} alt="more" className="dbImage" />
                    <h4 className="primaryDescription">
                      {t('clusterDetail.performance.advisor.ScanCluster')}
                    </h4>
                    <p className="secondaryDescription">
                      <b> Note: </b>
                      {t('clusterDetail.performance.advisor.RunWorkload')}
                    </p>
                    <Button
                      bsClass="btn btn-orange rescanBtn"
                      disabled={isUniversePaused || isUniverseUpdating}
                      onClick={handleScan}
                      data-placement="left"
                      title={
                        isUniversePaused
                          ? 'Universe Paused'
                          : isUniverseUpdating
                            ? 'Universe Updating'
                            : ''
                      }
                    >
                      <i className="fa fa-search-minus" aria-hidden="true"></i>
                      {t('clusterDetail.performance.advisor.ScanBtn')}
                    </Button>
                    <a
                      className="learnMoreLink"
                      href={EXTERNAL_LINKS.PERF_ADVISOR_DOCS_LINK}
                      target="_blank"
                      rel="noopener noreferrer"
                    >
                      <div className="learnMoreContent">
                        <img src={documentationIcon} alt="more" className="learnMoreImage" />
                        {t('clusterDetail.performance.advisor.LearnPerformanceAdvisor')}
                      </div>
                    </a>
                  </div>
                </div>
              </div>
            }
          />
        )}

        {/* This dialog is shown when there are no performance issues or when the scan fails  */}
        {isNonEmptyString(lastScanTime) && !recommendations.length && !isLastRunNotFound && (
          <YBPanelItem
            header={
              <div className="perfAdvisor">
                <div className="perfAdvisor__containerTitleGrid">
                  <div className="contentContainer">
                    <img src={EmptyTrayIcon} alt="more" />
                    <h4 className="primaryDescription">
                      {isPerfCallFail || scanStatus === LastRunStatus.FAILED
                        ? isNonEmptyString(errorMessage)
                          ? errorMessage
                          : t('common.wrong')
                        : t('clusterDetail.performance.advisor.Hurray')}
                    </h4>
                    <p className="secondaryDescription">
                      {isPerfCallFail || scanStatus === LastRunStatus.FAILED
                        ? t('clusterDetail.performance.advisor.DBScanFailed')
                        : t('clusterDetail.performance.advisor.NoPerformanceIssues')}
                    </p>
                    <Button
                      bsClass="btn btn-orange rescanBtn"
                      disabled={isUniversePaused || isUniverseUpdating}
                      onClick={handleScan}
                      data-placement="left"
                      title={
                        isUniversePaused
                          ? 'Universe Paused'
                          : isUniverseUpdating
                            ? 'Universe Updating'
                            : ''
                      }
                    >
                      <i className="fa fa-search-minus" aria-hidden="true"></i>
                      <RbacValidator
                        isControl
                        accessRequiredOn={{
                          onResource: universeUUID,
                          ...ApiPermissionMap.PERF_ADVISOR_START_MANUALLY
                        }}
                      >
                        {t('clusterDetail.performance.advisor.ReScanBtn')}
                      </RbacValidator>
                    </Button>
                  </div>
                </div>
              </div>
            }
          />
        )}

        {/* // This dialog is shown when there are recommendation results */}
        {isNonEmptyString(lastScanTime) && displayedRecomendations.length > 0 && !isLastRunNotFound && (
          <div>
            {(scanStatus === LastRunStatus.FAILED || errorMessage) && (
              <div className="scanFailureContainer">
                <img src={WarningIcon} alt="warning" className="warningIcon" />
                <span className="scanFailureMessage">
                  {isNonEmptyString(errorMessage)
                    ? t('clusterDetail.performance.advisor.DBScanFailed') + ':' + errorMessage
                    : t('clusterDetail.performance.advisor.DBScanFailed')}
                </span>
              </div>
            )}
            <div className="perfAdvisor__containerTitleFlex">
              <h5 className="numRecommendations">
                {displayedRecomendations.length} {recommendationLabel}
              </h5>
              <p className="scanTime">
                {t('clusterDetail.performance.advisor.ScanTime')}
                {t('clusterDetail.performance.advisor.Separator')}
                {ybFormatDate(new Date())}
              </p>
              <RbacValidator
                isControl
                accessRequiredOn={{
                  onResource: universeUUID,
                  ...ApiPermissionMap.PERF_ADVISOR_START_MANUALLY
                }}
              >
                <YBButton
                  btnClass="btn btn-orange rescanBtnRecPage"
                  disabled={isUniversePaused || isUniverseUpdating}
                  btnText="Re-Scan"
                  btnIcon="fa fa-search-minus"
                  onClick={handleScan}
                  data-placement="left"
                  title={
                    isUniversePaused
                      ? 'Universe Paused'
                      : isUniverseUpdating
                        ? 'Universe Updating'
                        : ''
                  }
                />
              </RbacValidator>
            </div>
            <div className="perfAdvisor__containerRecommendationFlex">
              <YBSelect
                onChange={handleDbSelection}
                value={databaseSelection}
                className="filterDropdowns"
                inputProps={{
                  'data-testid': `PerfAdvisor-DBSelect`
                }}
              >
                {databaseOptionList}
              </YBSelect>
              <YBSelect
                onChange={handleSuggestionTypeSelection}
                value={suggestionType}
                className="filterDropdowns"
                inputProps={{
                  'data-testid': `PerfAdvisor-SuggestionTypeSelect`
                }}
              >
                {recommendationTypes.map((type) => (
                  <MenuItem key={`suggestion-${type}`} value={type}>
                    {t(`clusterDetail.performance.suggestionTypes.${TranslationTypeMap[type]}`)}
                  </MenuItem>
                ))}
              </YBSelect>
            </div>
            {displayedRecomendations.map((rec) => (
              <>
                <RecommendationBox
                  key={rec.key}
                  idKey={rec.key}
                  type={rec.data.type}
                  data={rec.data}
                  resolved={!!rec.isResolved}
                  onResolve={handleResolve}
                />
              </>
            ))}
          </div>
        )}
      </RbacValidator>
    </div>
  );
};
