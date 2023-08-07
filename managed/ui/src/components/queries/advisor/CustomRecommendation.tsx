import { FC } from 'react';
import { useTranslation } from 'react-i18next';
import { EXTERNAL_LINKS } from '../helpers/constants';
import { CustomRecommendation, RecommendationType } from '../../../redesign/utils/dtos';
import lightBulbIcon from '../images/lightbulb.svg';
import './styles.scss';

export const CustomRecommendations: FC<CustomRecommendation> = ({ summary, suggestion, type }) => {
  const { t } = useTranslation();
  let recommendationLink = EXTERNAL_LINKS.PERF_ADVISOR_DOCS_LINK;
  if (type === RecommendationType.CPU_USAGE) {
    recommendationLink = EXTERNAL_LINKS.CPU_SKEW_AND_USAGE;
  } else if (RecommendationType.HOT_SHARD) {
    recommendationLink = EXTERNAL_LINKS.HOT_SHARD;
  } else if (RecommendationType.REJECTED_CONNECTIONS) {
    recommendationLink = EXTERNAL_LINKS.REJECTED_CONNECTIONS;
  }

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
              href={recommendationLink}
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
