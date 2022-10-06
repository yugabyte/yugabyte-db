import React, { FC } from 'react';
import { useTranslation } from 'react-i18next';

import lightBulbIcon from '../images/lightbulb.svg';
import { EXTERNAL_LINKS } from '../helpers/const';
import { CpuUsageRecommendation } from '../../../redesign/helpers/dtos';
import './styles.scss';

export const CpuUsage: FC<CpuUsageRecommendation> = ({ summary }) => {
  const { t } = useTranslation();
  return (
    <div>
      <div className="recommendationBox">
        <span> {summary} </span>
        <div className="recommendationAdvice">
          <img src={lightBulbIcon} alt="more" className="learnMoreImage" />
          <span className="learnPerfAdvisorText">
            {t('clusterDetail.performance.advisor.Recommendation')}
            {t('clusterDetail.performance.advisor.Separator')}
            {t('clusterDetail.performance.advisor.RebalanceAndTroubleshoot')}
            <a
              target="_blank"
              className="learnSchemaSuggestion"
              href={EXTERNAL_LINKS.PERF_ADVISOR_DOCS_LINK}
              rel="noopener noreferrer"
            >
              {t('clusterDetail.performance.advisor.LearnHow')}
            </a>
          </span>
        </div>
      </div>
    </div>
  )
}
