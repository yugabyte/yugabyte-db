import React, { FC } from 'react';
import { useTranslation } from 'react-i18next';

import lightBulbIcon from '../images/lightbulb.svg';
import { EXTERNAL_LINKS } from '../helpers/const';
import { CustomRecommendation } from '../../../redesign/utils/dtos';
import './styles.scss';

export const CustomRecommendations: FC<CustomRecommendation> = ({ summary, suggestion }) => {
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
            {suggestion}
            <a
              target="_blank"
              className="learnRecommendationSuggestions"
              href={EXTERNAL_LINKS.CPU_SKEW_AND_USAGE}
              rel="noopener noreferrer"
            >
              {t('clusterDetail.performance.advisor.LearnHow')}
            </a>
          </span>
        </div>
      </div>
    </div>
  );
};
