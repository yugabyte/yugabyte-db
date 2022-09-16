import React, { useState, FC, ReactNode, useCallback, Fragment } from 'react';
import { useTranslation } from 'react-i18next';
import { Panel } from 'react-bootstrap';

import { IndexSuggestion } from './advisor/IndexSuggestion';
import { SchemaSuggestion } from './advisor/SchemaSuggestion';
import { QueryLoadSkew } from './advisor/QueryLoadSkew';
import { ConnectionSkew } from './advisor/ConnectionSkew';
import { CpuSkew } from './advisor/CpuSkew';
import { CpuUsage } from './advisor/CpuUsage';
import { QueryData } from './helpers/perfQueryUtils';
import { YBPanelItem } from '../panels';
import { RecommendationTypeEnum } from '../../redesign/helpers/dtos';
import './RecommendationBox.scss';

interface RecommendationProps {
  type: RecommendationTypeEnum;
  data: QueryData;
  key: string;
}

const getRecommendation = (type: RecommendationTypeEnum, summary: ReactNode | string, data: QueryData) => {
  if (type === RecommendationTypeEnum.IndexSuggestion) {
    const indexSuggestionData = data.table?.data ?? [];
    return (
      <IndexSuggestion summary={summary} data={indexSuggestionData} />
    )
  }
  if (type === RecommendationTypeEnum.SchemaSuggestion) {
    const schemaSuggestionData = data.table?.data ?? [];
    return <SchemaSuggestion data={schemaSuggestionData} summary={summary} />;
  }
  if (type === RecommendationTypeEnum.QueryLoadSkew) {
    const graphData = data.graph;
    const querySkewData = {
      maxNodeName: data.target,
      percentDiff: data.indicator,
      maxNodeDistribution: {
        numSelect: graphData?.max.num_select ?? 0,
        numInsert: graphData?.max.num_insert ?? 0,
        numUpdate: graphData?.max.num_update ?? 0,
        numDelete: graphData?.max.num_delete ?? 0
      },
      otherNodesDistribution: {
        numSelect: graphData?.other.num_select ?? 0,
        numInsert: graphData?.other.num_insert ?? 0,
        numUpdate: graphData?.other.num_update ?? 0,
        numDelete: graphData?.other.num_delete ?? 0
      }
    };
    return <QueryLoadSkew data={querySkewData} summary={summary} />;
  }
  if (type === RecommendationTypeEnum.ConnectionSkew) {
    const connectionSkewData = {
      maxNodeName: data.target,
      maxNodeValue: data.graph?.max.value ?? 0,
      otherNodesAvgValue: data.graph?.other.value ?? 0,
    };
    return <ConnectionSkew data={connectionSkewData} summary={summary} />;
  }
  if (type === RecommendationTypeEnum.CpuSkew) {
    const cpuSkewData = {
      maxNodeName: data.target,
      maxNodeValue: data.graph?.max.value ?? 0,
      otherNodesAvgValue: data.graph?.other.value ?? 0
    };
    return <CpuSkew data={cpuSkewData} summary={summary} />;
  }
  if (type === RecommendationTypeEnum.CpuUsage) {
    return <CpuUsage summary={summary} />;
  }
  return null;
};
export const RecommendationBox: FC<RecommendationProps> = ({ key, type, data }) => {
  const { t } = useTranslation();
  const getTypeTagColor = useCallback(
    (recommendationType: RecommendationTypeEnum) => {
      switch (recommendationType) {
        case RecommendationTypeEnum.IndexSuggestion:
          return (
            <span className="recommendationTitle tagGreen">
              {t('clusterDetail.performance.typeTag.IndexSuggestion')}
            </span>
          );
        case RecommendationTypeEnum.SchemaSuggestion:
          return (
            <span className="recommendationTitle tagGreen">
              {t('clusterDetail.performance.typeTag.SchemaSuggestion')}
            </span>
          );
        case RecommendationTypeEnum.QueryLoadSkew:
          return (
            <span className="recommendationTitle tagBlue">
              {t('clusterDetail.performance.typeTag.QueryLoadSkew')}
            </span>
          );
        case RecommendationTypeEnum.ConnectionSkew:
          return (
            <span className="recommendationTitle tagBlue">
              {t('clusterDetail.performance.typeTag.ConnectionSkew')}
            </span>
          );
        case RecommendationTypeEnum.CpuSkew:
          return (
            <span className="recommendationTitle tagBlue">
              {t('clusterDetail.performance.typeTag.CpuSkew')}
            </span>
          );
        case RecommendationTypeEnum.CpuUsage:
          return (
            <span className="recommendationTitle tagBlue">
              {t('clusterDetail.performance.typeTag.CpuUsage')}
            </span>
          );
        default:
          return null;
      }
    }, [t]
  );
  const getSummaryContent = (recommendationType: RecommendationTypeEnum, recommendationData: QueryData) => {
    if (recommendationType === RecommendationTypeEnum.IndexSuggestion) {
      const { indicator: numIndexes, target: databaseName } = recommendationData;
      return (
        <Fragment>
          <span className="recommendationHeaderDescription">
            {`We found ${numIndexes} indexes in database `}
            <b>{databaseName}</b>
            {' that were never used.'}
          </span>
        </Fragment>
      )
    }
    if (recommendationType === RecommendationTypeEnum.SchemaSuggestion) {
      const { indicator: numIndexes, target: databaseName } = recommendationData;
      return (
        <Fragment>
          <span className="recommendationHeaderDescription">
            {`We found ${numIndexes} indexes in database `}
            <b> {databaseName} </b>
            {' where using range sharding instead of hash can improve query speed.'}
          </span>
        </Fragment>
      )
    }
    if (recommendationType === RecommendationTypeEnum.QueryLoadSkew) {
      const { target: nodeName, indicator: percent, setSize: otherNodes } = recommendationData;
      return (
        <Fragment>
          <span className="recommendationHeaderDescription">
            {`Node `}
            <b> {nodeName} </b>
            {`processed ${percent}% more queries than the other ${otherNodes} nodes`}
          </span>
        </Fragment>
      )
    }
    if (recommendationType === RecommendationTypeEnum.ConnectionSkew) {
      const { target: nodeName, indicator: percent, setSize: otherNodes } = recommendationData;
      return (
        <Fragment>
          <span className="recommendationHeaderDescription">
            {`Node `}
            <b> {nodeName} </b>
            {` handled ${percent}% more connections than the other ${otherNodes} nodes in the past hour.`}
          </span>
        </Fragment>
      )
    }
    if (recommendationType === RecommendationTypeEnum.CpuSkew) {
      const { target: nodeName, indicator: percent, setSize: otherNodes } = recommendationData;
      return (
        <Fragment>
          <span className="recommendationHeaderDescription">
            {`Node `}
            <b> {nodeName} </b>
            {` CPU use is ${percent}% greater than the ${otherNodes} nodes in the past hour.`}
          </span>
        </Fragment>
      )
    }
    if (recommendationType === RecommendationTypeEnum.CpuUsage) {
      const { target: nodeName, indicator: percent, timeDurationSec: duration } = recommendationData;
      const minuteDuration = Math.round((duration ?? 0) / 60);
      return (
        <Fragment>
          <span className="recommendationHeaderDescription">
            {`Node `}
            <b> {nodeName} </b>
            {` CPU use exceeded ${percent}% for ${minuteDuration} mins.`}
          </span>
        </Fragment>
      )
    }
    return null;
  }

  const [isPanelExpanded, setPanelExpanded] = useState(false);
  return (
    <YBPanelItem
      className="perfAdvisorPanel"
      body={
        <Panel
          id={`${key}-recommendation`}
          eventKey={`${key}-recommendation`}
          defaultExpanded={isPanelExpanded}
          className="recommendationContainer"
        >
          <Panel.Heading>
            <Panel.Title
              toggle
              onClick={() => setPanelExpanded(!isPanelExpanded)}
            >
              {getTypeTagColor(type)}
              {!isPanelExpanded && getSummaryContent(type, data)}
            </Panel.Title>
          </Panel.Heading>
          <Panel.Body collapsible>{isPanelExpanded && getRecommendation(type, getSummaryContent(type, data), data)}</Panel.Body>
        </Panel>
      } noBackground
    />
  );
}
