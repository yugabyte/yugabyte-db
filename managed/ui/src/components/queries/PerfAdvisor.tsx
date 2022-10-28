import React, { useState, FC, ReactNode, useEffect, useCallback } from 'react';
import { useTranslation } from 'react-i18next';
import { useSelector } from 'react-redux';
import { FormControl, Button } from 'react-bootstrap';

import dbSettingsIcon from './images/db-settings.svg';
import documentationIcon from './images/documentation.svg';
import EmptyTrayIcon from './images/empty-tray.svg';
import { RecommendationBox } from './RecommendationBox';
import { YBPanelItem } from '../panels';
import { EXTERNAL_LINKS } from './helpers/const';
import {
  QueryData,
  handleIndexSuggestionRequest,
} from './helpers/perfQueryUtils';
import { YBButton } from '../common/forms/fields';
import { intlFormat } from 'date-fns';
import { YBLoading } from '../../components/common/indicators';
import { RecommendationTypeEnum } from '../../redesign/helpers/dtos';
import './PerfAdvisor.scss';

interface RecommendationDetailProps {
  data: QueryData;
  key: string;
  resolved?: boolean;
}

const mockResponseQueryDistribution: QueryData = {
  type: RecommendationTypeEnum.SchemaSuggestion,
  target: "yugabyte",
  indicator: 2,
  table: {
    data: [{
      current_database: 'yugabyte',
      index_command: 'CREATE UNIQUE INDEX table1_pkey ON public.table1 USING lsm (column1 HASH)',
      index_name: 'table1_pkey',
      table_name: 'table1'
    },
    {
      current_database: 'yugabyte',
      index_command: 'CREATE INDEX table1_column2_idx ON public.table1 USING lsm (column2 HASH)',
      index_name: 'table1_column2_idx',
      table_name: 'table1'
    }

    ],
  }
};
const maxNodeDetasils = {
  num_select: 100,
  num_insert: 800,
  num_update: 300,
  num_delete: 400
}
const otherNodesDetails = {
  num_select: 100,
  num_insert: 200,
  num_update: 300,
  num_delete: 200
}
const mockResponseQuerySkew: QueryData = {
  type: RecommendationTypeEnum.QueryLoadSkew,
  target: 'cluster-n1',
  setSize: 2,
  indicator: 80,
  graph: {
    max: {
      ...maxNodeDetasils
    },
    other: {
      ...otherNodesDetails
    }
  }
};

const mockConnectionSkewRecommendation: QueryData = {
  type: RecommendationTypeEnum.ConnectionSkew,
  target: 'cluster-n1',
  setSize: 2,
  indicator: 60,
  graph: {
    max: {
      value: 4
    },
    other: {
      value: 1.75
    }
  }
}

const mockCPUSkewRecommendation: QueryData = {
  type: RecommendationTypeEnum.CpuSkew,
  target: 'puppy-food-n1',
  setSize: 2,
  indicator: 65,
  graph: {
    max: {
      value: 65.432
    },
    other: {
      value: 20.789
    }
  }
}

const mockCPUUsageRecommendation: QueryData = {
  type: RecommendationTypeEnum.CpuUsage,
  target: 'cluster-n1',
  indicator: 80.0,
  timeDurationSec: 600
}

/** Generates unique string based on recommendation type for the purpose of setting unique key on React component */
const generateUniqueId = (type: string, ...identifiers: string[]) => {
  return `${type.replaceAll(' ', '')}-${identifiers
    .map((x) => x.replaceAll(' ', ''))
    .join('-')}-${Math.random().toString(16).slice(2)}`;
};

// These are the only two recommendation types related to DB
const DATABASE_TYPE_SUGGESTIONS = [
  RecommendationTypeEnum.IndexSuggestion,
  RecommendationTypeEnum.SchemaSuggestion
];

export const PerfAdvisor: FC = () => {
  const { t } = useTranslation();
  const [isScanningLoading, setIsScanningLoading] = useState(false);
  const [lastScanTime, setLastScanTime] = useState('');
  const [recommendations, setRecommendations] = useState<RecommendationDetailProps[]>([]);
  const [databaseSelection, setDatabaseSelection] = useState('');
  const [databaseOptionList, setDatabaseOptionList] = useState<ReactNode[]>([]);
  const [suggestionType, setSuggestionType] = useState(RecommendationTypeEnum.All);
  const currentUniverse = useSelector((state: any) => state.universe.currentUniverse);
  const universeUUID = currentUniverse?.data?.universeUUID;
  const isUniversePaused: boolean = currentUniverse?.data?.universeDetails?.universePaused;

  const handleDbSelection = (event: any) => {
    setSuggestionType(RecommendationTypeEnum.All);
    setDatabaseSelection(event.target.value);
  };

  const handleSuggestionTypeSelection = (e: any) => {
    const nextSuggestionType: RecommendationTypeEnum =
      RecommendationTypeEnum[(e.target.value as unknown) as RecommendationTypeEnum];
    setSuggestionType(nextSuggestionType);
  };

  const processRecommendationData = useCallback(
    (data: QueryData[]) => {
      const recommendationArr: RecommendationDetailProps[] = [];
      const databasesInRecommendations: string[] = [];
      data.forEach((recommendation) => {
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

    // TODO: Once API is ready, need to group all promise call here
    const promiseArr = [
      handleIndexSuggestionRequest(universeUUID),
    ];
    const results = await Promise.allSettled<Promise<QueryData | null> | Promise<QueryData[] | null>>(promiseArr);
    const recommendationDataArray: QueryData[] =
      results
        .flatMap((promiseRes) => {
          if (promiseRes.status === 'fulfilled' && promiseRes.value) {
            return promiseRes.value;
          }
          return null;
        })
        .filter(Boolean) as QueryData[];
    // Mocking API response as it is not ready
    // TODO: Need to remove this once APIs are available
    recommendationDataArray.push(mockResponseQueryDistribution);
    recommendationDataArray.push(mockResponseQuerySkew);
    recommendationDataArray.push(mockConnectionSkewRecommendation);
    recommendationDataArray.push(mockCPUSkewRecommendation);
    recommendationDataArray.push(mockCPUUsageRecommendation);
    const newRecommendations = processRecommendationData(recommendationDataArray);
    setRecommendations(newRecommendations);
    localStorage.setItem(universeUUID, JSON.stringify({
      data: newRecommendations,
      lastUpdated: currentScanTime
    }));
    setIsScanningLoading(false);
  }


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


  const filteredByDatabaseRecommendations = recommendations.filter((rec) => {
    if (databaseSelection) {
      const isMatchingDb = DATABASE_TYPE_SUGGESTIONS.includes(rec.data.type) && rec.data.target === databaseSelection;
      return isMatchingDb;
    }
    return true;
  });
  const displayedRecomendations = filteredByDatabaseRecommendations.filter((rec) => {
    if (suggestionType !== RecommendationTypeEnum.All) {
      return rec.data.type === suggestionType;
    }
    return true;
  });

  const recommendationTypeList: Record<string, boolean> = {
    [RecommendationTypeEnum.All.toString()]: true
  };
  filteredByDatabaseRecommendations.forEach((curr: RecommendationDetailProps) => {
    recommendationTypeList[curr.data.type] = true;
  });
  const recommendationTypes = Object.keys(recommendationTypeList);
  const recommendationLabel = displayedRecomendations?.length > 0 ? 'Recommendations' : 'Recommendation';

  if (isScanningLoading) {
    return (<YBPanelItem
      header={
        <div className="perfAdvisor__containerTitleGrid">
          <YBLoading text="Scanning" />
        </div>
      }
    />);
  }

  return (
    // This dialog is shown when the user visits performance advisor for first time 
    // (or) when a user visits performance advisor in a new session
    <div className="parentPerfAdvisor">
      {!lastScanTime && <YBPanelItem
        header={
          <div className="perfAdvisor">
            <div className="perfAdvisor__containerTitleGrid">
              <div className="contentContainer">
                <img src={dbSettingsIcon} alt="more" className="dbImage" />
                <h4 className="primaryDescription">{t('clusterDetail.performance.advisor.ScanCluster')}</h4>
                <p className="secondaryDescription"> <b> Note: </b>{t('clusterDetail.performance.advisor.RunWorkload')}</p>
                <Button
                  bsClass='btn btn-orange rescanBtn'
                  disabled={!!isUniversePaused}
                  onClick={handleScan}
                  data-toggle={!!isUniversePaused ? "tooltip" : ""}
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
      />}

      {/* // This dialog is shown when there are no performance scanning results  */}
      {lastScanTime && !recommendations.length &&
        <YBPanelItem
          header={
            <div className="perfAdvisor">
              <div className="perfAdvisor__containerTitleGrid">
                <div className="contentContainer">
                  <img src={EmptyTrayIcon} alt="more" />
                  <h4 className="primaryDescription"> {t('clusterDetail.performance.advisor.Hurray')}</h4>
                  <p className="secondaryDescription"> {t('clusterDetail.performance.advisor.NoPerformanceIssues')}</p>
                  <Button
                    bsClass='btn btn-orange rescanBtn'
                    disabled={!!isUniversePaused}
                    onClick={handleScan}
                    data-toggle={!!isUniversePaused ? "tooltip" : ""}
                    data-placement="left"
                    title="Universe Paused"

                  >
                    <i className="fa fa-search-minus" aria-hidden="true"></i>
                    {t('clusterDetail.performance.advisor.ReScanBtn')}
                  </Button>
                  <div className="ticketContent">
                    <b>Note:</b>&nbsp; If this doesn't look right to you,&nbsp;
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
      }

       {/* // This dialog is shown when there are multiple recommendation options */}
      {displayedRecomendations.length &&
        <div>
          <div className="perfAdvisor__containerTitleFlex">
            <h5 className="numRecommendations">{displayedRecomendations.length} {recommendationLabel}</h5>
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
            <FormControl componentClass="select"
              onChange={handleDbSelection}
              value={databaseSelection}
              className="filterDropdowns">
              {databaseOptionList}
            </FormControl>
            <FormControl componentClass="select"
              onChange={handleSuggestionTypeSelection}
              value={suggestionType}
              className="filterDropdowns">
              {recommendationTypes.map((type) => (
                <option key={`suggestion-${type}`} value={type}>
                  {t(`clusterDetail.performance.suggestionTypes.${type}`)}
                </option>
              ))}
            </FormControl>
          </div>
          {displayedRecomendations.map((rec) => (
            <RecommendationBox
              key={rec.key}
              type={rec.data.type}
              data={rec.data}
            />
          ))}
        </div>
      }
    </div>
  );
};
