import React, { useState, FC, ReactNode, useEffect, useCallback } from 'react';
import { useTranslation } from 'react-i18next';
import { useQuery } from 'react-query';
import { useSelector } from 'react-redux';
import { FormControl, Button } from 'react-bootstrap';
import { intlFormat } from 'date-fns';
import { RecommendationBox } from './RecommendationBox';
import { YBPanelItem } from '../panels';
import { YBLoading } from '../../components/common/indicators';
import { YBButton } from '../common/forms/fields';
import { performanceRecommendationApi, QUERY_KEY } from './helpers/api';
import { formatPerfRecommendationsData } from './helpers/utils';
import { EXTERNAL_LINKS } from './helpers/const';
import { isNonEmptyString } from '../../utils/ObjectUtils';
import {
  IndexAndShardingRecommendationData,
  PerfRecommendationData,
  RecommendationType,
  SortDirection
} from '../../redesign/utils/dtos';
import dbSettingsIcon from './images/db-settings.svg';
import documentationIcon from './images/documentation.svg';
import EmptyTrayIcon from './images/empty-tray.svg';
import './PerfAdvisor.scss';

interface RecommendationDetailProps {
  data: PerfRecommendationData | IndexAndShardingRecommendationData;
  key: string;
  resolved?: boolean;
}

const translationTypeMap = {
  ALL: 'All',
  RANGE_SHARDING: 'SchemaSuggestion',
  QUERY_LOAD_SKEW: 'QueryLoadSkew',
  CONNECTION_SKEW: 'ConnectionSkew',
  CPU_SKEW: 'CpuSkew',
  CPU_USAGE: 'CpuUsage',
  UNUSED_INDEX: 'IndexSuggestion',
  HOT_SHARD: 'HotShard'
};

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
  const { t } = useTranslation();
  const [isScanningLoading, setIsScanningLoading] = useState<Boolean>(false);
  const [lastScanTime, setLastScanTime] = useState('');
  const [recommendations, setRecommendations] = useState<RecommendationDetailProps[]>([]);
  const [databaseSelection, setDatabaseSelection] = useState('');
  const [databaseOptionList, setDatabaseOptionList] = useState<ReactNode[]>([]);
  const [suggestionType, setSuggestionType] = useState(RecommendationType.ALL);
  const [isPerfCallFail, setIsPerfCallFail] = useState<Boolean>(false);
  const currentUniverse = useSelector((state: any) => state.universe.currentUniverse);
  const universeUUID = currentUniverse?.data?.universeUUID;
  const isUniversePaused: boolean = currentUniverse?.data?.universeDetails?.universePaused;

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
  //fetch run time configs
  const { refetch: perfRecommendationsRefetch } = useQuery(QUERY_KEY.fetchPerfRecommendations, () =>
    performanceRecommendationApi.fetchPerfRecommendationsList(queryParams)
  );

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
        <option value={''} key={'all-databases'}>
          {t('clusterDetail.performance.advisor.AllDatabases')}
        </option>,
        [...databasesInRecommendations].map((databaseName: string) => (
          <option value={databaseName} key={`ysql-${databaseName}`}>
            {databaseName}
          </option>
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
        <option value={''} key={'all-databases'}>
          {t('clusterDetail.performance.advisor.AllDatabases')}
        </option>,
        [...databasesInRecommendations].map((databaseName: string) => (
          <option value={databaseName} key={`ysql-${databaseName}`}>
            {databaseName}
          </option>
        ))
      ]);
    },
    [t]
  );

  const handleScan = async () => {
    const currentDate = new Date();
    const currentScanTime = intlFormat(currentDate, {
      year: 'numeric',
      month: '2-digit',
      day: '2-digit',
      hour: 'numeric',
      minute: '2-digit'
    });
    setLastScanTime(currentScanTime);
    setIsScanningLoading(true);

    const { data, isError } = await perfRecommendationsRefetch();
    setIsPerfCallFail(isError);
    const perfRecommendations = formatPerfRecommendationsData(data);
    const newRecommendations = processRecommendationData(perfRecommendations);
    setRecommendations(newRecommendations);
    localStorage.setItem(
      universeUUID,
      JSON.stringify({
        data: newRecommendations,
        lastUpdated: currentScanTime
      })
    );
    setIsScanningLoading(false);
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
          setLastScanTime(
            intlFormat(new Date(previousScanResult?.lastUpdated), {
              year: 'numeric',
              month: '2-digit',
              day: '2-digit',
              hour: 'numeric',
              minute: '2-digit'
            })
          );
        }
      }
    };
    void checkForPreviousScanData();
  }, [universeUUID]); // eslint-disable-line react-hooks/exhaustive-deps

  useEffect(() => {
    return () => {
      localStorage.setItem(universeUUID, '');
    };
  }, [universeUUID]);

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
    displayedRecomendations?.length > 0 ? 'Recommendations' : 'Recommendation';

  if (isScanningLoading) {
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
    // This dialog is shown when the user visits performance advisor for first time
    // (or) when a user visits performance advisor in a new session
    <div className="parentPerfAdvisor">
      {!isNonEmptyString(lastScanTime) && (
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
                    disabled={!!isUniversePaused}
                    onClick={handleScan}
                    data-toggle={isUniversePaused ? 'tooltip' : ''}
                    data-placement="left"
                    title="Universe Paused"
                  >
                    <i className="fa fa-search-minus" aria-hidden="true"></i>
                    {t('clusterDetail.performance.advisor.ScanBtn')}
                  </Button>
                  <a className="learnMoreLink" href={EXTERNAL_LINKS.PERF_ADVISOR_DOCS_LINK}>
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

      {/* // This dialog is shown when there are no performance scanning results  */}
      {lastScanTime && !recommendations.length && (
        <YBPanelItem
          header={
            <div className="perfAdvisor">
              <div className="perfAdvisor__containerTitleGrid">
                <div className="contentContainer">
                  <img src={EmptyTrayIcon} alt="more" />
                  <h4 className="primaryDescription">
                    {t('clusterDetail.performance.advisor.Hurray')}
                  </h4>
                  <p className="secondaryDescription">
                    {isPerfCallFail
                      ? t('clusterDetail.performance.advisor.FailedToLoad')
                      : t('clusterDetail.performance.advisor.NoPerformanceIssues')}
                  </p>
                  <Button
                    bsClass="btn btn-orange rescanBtn"
                    disabled={!!isUniversePaused}
                    onClick={handleScan}
                    data-toggle={isUniversePaused ? 'tooltip' : ''}
                    data-placement="left"
                    title="Universe Paused"
                  >
                    <i className="fa fa-search-minus" aria-hidden="true"></i>
                    {t('clusterDetail.performance.advisor.ReScanBtn')}
                  </Button>
                  <div className="ticketContent">
                    <b>Note:</b>&nbsp; {"If this doesn't look right to you,"}&nbsp;
                    <a className="ticketLink" href={EXTERNAL_LINKS.SUPPORT_TICKET_LINK}>
                      {t('clusterDetail.performance.advisor.OpenSupportTicket')}
                    </a>
                    &nbsp;and let us know
                  </div>
                </div>
              </div>
            </div>
          }
        />
      )}

      {/* // This dialog is shown when there are multiple recommendation options */}
      {isNonEmptyString(lastScanTime) && displayedRecomendations.length > 0 && (
        <div>
          <div className="perfAdvisor__containerTitleFlex">
            <h5 className="numRecommendations">
              {displayedRecomendations.length} {recommendationLabel}
            </h5>
            <p className="scanTime">
              {t('clusterDetail.performance.advisor.ScanTime')}
              {t('clusterDetail.performance.advisor.Separator')}
              {lastScanTime}
            </p>
            <YBButton
              btnClass="btn btn-orange rescanBtnRecPage"
              btnText="Re-Scan"
              btnIcon="fa fa-search-minus"
              onClick={handleScan}
            />
          </div>
          <div className="perfAdvisor__containerRecommendationFlex">
            <FormControl
              componentClass="select"
              onChange={handleDbSelection}
              value={databaseSelection}
              className="filterDropdowns"
            >
              {databaseOptionList}
            </FormControl>
            <FormControl
              componentClass="select"
              onChange={handleSuggestionTypeSelection}
              value={suggestionType}
              className="filterDropdowns"
            >
              {recommendationTypes.map((type) => (
                <option key={`suggestion-${type}`} value={type}>
                  {t(`clusterDetail.performance.suggestionTypes.${translationTypeMap[type]}`)}
                </option>
              ))}
            </FormControl>
          </div>
          {displayedRecomendations.map((rec) => (
            <RecommendationBox key={rec.key} type={rec.data.type} data={rec.data} />
          ))}
        </div>
      )}
    </div>
  );
};
