import { FC } from 'react';
import { useTranslation } from 'react-i18next';
import { BootstrapTable, TableHeaderColumn } from 'react-bootstrap-table';

import documentationIcon from '../images/documentation.svg';
import lightBulbIcon from '../images/lightbulb.svg';
import { EXTERNAL_LINKS } from '../helpers/constants';
import { IndexSchemaRecommendation } from '../../../redesign/utils/dtos';
import './styles.scss';

export const IndexSuggestion: FC<IndexSchemaRecommendation> = ({ data, summary }) => {
  const { t } = useTranslation();
  if (!data?.length) {
    return null;
  }

  const getIndexName = function (cell: any, row: any) {
    return <>{row.recommendationInfo.index_name}</>;
  };

  const getTableName = function (cell: any, row: any) {
    return <>{row.recommendationInfo.table_name}</>;
  };

  const getIndexCommand = function (cell: any, row: any) {
    return <>{row.recommendationInfo.index_command}</>;
  };

  return (
    <div>
      <div className="recommendationBox">
        <div className="recommendationBoxContent">
          <span> {summary} </span>
          <a
            className="learnPerfAdvisor"
            href={EXTERNAL_LINKS.UNUSED_INDEX}
            target="_blank"
            rel="noopener noreferrer"
          >
            <img src={documentationIcon} alt="more" className="learnMoreImage" />
            <p className="learnPerfAdvisorText">
              {t('clusterDetail.performance.advisor.IndexPerformanceTuning')}
            </p>
          </a>
        </div>
        <div className="recommendationAdvice">
          <img src={lightBulbIcon} alt="more" className="learnMoreImage" />
          <p className="learnPerfAdvisorText">
            {t('clusterDetail.performance.advisor.Recommendation')}
            {t('clusterDetail.performance.advisor.Separator')}
            {t('clusterDetail.performance.advisor.DropIndex')}
          </p>
        </div>
      </div>
      <div className="recommendationClass">
        <BootstrapTable data={data} pagination={data?.length > 10}>
          <TableHeaderColumn
            dataField="index_name"
            dataFormat={getIndexName}
            isKey={true}
            width="17%"
            tdStyle={{ whiteSpace: 'normal', wordWrap: 'break-word' }}
            columnClassName="no-border"
          >
            Unused Index Name
          </TableHeaderColumn>
          <TableHeaderColumn
            dataField="table_name"
            dataFormat={getTableName}
            width="13%"
            tdStyle={{ whiteSpace: 'normal', wordWrap: 'break-word' }}
            columnClassName="no-border"
          >
            Table
          </TableHeaderColumn>
          <TableHeaderColumn
            dataField="index_command"
            dataFormat={getIndexCommand}
            width="70%"
            tdStyle={{ whiteSpace: 'normal', wordWrap: 'break-word' }}
            columnClassName="no-border"
          >
            Create Command
          </TableHeaderColumn>
        </BootstrapTable>
      </div>
    </div>
  );
};
