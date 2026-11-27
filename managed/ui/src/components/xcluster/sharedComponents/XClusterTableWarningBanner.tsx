import { ReactNode } from 'react';
import { makeStyles } from '@material-ui/core';
import { OperationBannerVariant, YBOperationBanner } from '@yugabyte-ui-library/core';
import { Trans, useTranslation } from 'react-i18next';

import {
  XCLUSTER_DR_TABLE_STATUS_DOCUMENTATION_URL,
  XCLUSTER_REPLICATION_TABLE_STATUS_DOCUMENTATION_URL
} from '../constants';
import { TableCountsOfConcern } from '../ReplicationUtils';

interface XClusterTableWarningBannerProps {
  tableCountsOfConcern: TableCountsOfConcern;
  numTablesAboveLagThreshold: number;
  isDrInterface: boolean;
  onViewTables: () => void;
  // Rendered next to the Missing Op ID step. Callers own the RBAC check and enabled state.
  restartButton: ReactNode;
}

const useStyles = makeStyles((theme) => ({
  // The banner renders `description` inside a <p>, so the steps are spans rather than an <ol>.
  step: {
    display: 'flex',
    alignItems: 'center',
    gap: theme.spacing(1.5),

    minHeight: '24px',
    '& + &': {
      marginTop: theme.spacing(1)
    }
  },
  link: {
    color: theme.palette.primary[600],
    textDecoration: 'none',

    '&:hover, &:focus': {
      color: theme.palette.primary[600],
      textDecoration: 'none'
    }
  },
  // Switches tabs in place rather than navigating, so it is a button styled as a link.
  linkButton: {
    padding: 0,
    border: 'none',
    background: 'none',
    font: 'inherit',
    cursor: 'pointer'
  }
}));

const TRANSLATION_KEY_PREFIX = 'clusterDetail.xCluster.shared.banner';
export const XCLUSTER_TABLE_WARNING_BANNER_TEST_ID = 'xcluster-table-warning-banner';

export const XClusterTableWarningBanner = ({
  tableCountsOfConcern,
  numTablesAboveLagThreshold,
  isDrInterface,
  onViewTables,
  restartButton
}: XClusterTableWarningBannerProps) => {
  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });
  const classes = useStyles();

  const hasTablesOfConcern = tableCountsOfConcern.uniqueTableCount > 0;
  if (!hasTablesOfConcern && numTablesAboveLagThreshold === 0) {
    return null;
  }

  if (!hasTablesOfConcern) {
    return (
      <YBOperationBanner
        title={t('warningTitle')}
        actionText={t('viewTables')}
        onAction={onViewTables}
        dataTestId={XCLUSTER_TABLE_WARNING_BANNER_TEST_ID}
        variant={OperationBannerVariant.Warning}
        message={t(
          isDrInterface
            ? 'replicationLagAboveThreshold.dr'
            : 'replicationLagAboveThreshold.xClusterReplication'
        )}
      />
    );
  }

  const tableStatusDocsLink = (
    <a
      className={classes.link}
      href={
        isDrInterface
          ? XCLUSTER_DR_TABLE_STATUS_DOCUMENTATION_URL
          : XCLUSTER_REPLICATION_TABLE_STATUS_DOCUMENTATION_URL
      }
      target="_blank"
      rel="noopener noreferrer"
    />
  );
  return (
    <YBOperationBanner
      title={t('warningTitle')}
      dataTestId={XCLUSTER_TABLE_WARNING_BANNER_TEST_ID}
      variant={OperationBannerVariant.Error}
      message={
        <Trans
          i18nKey={`${TRANSLATION_KEY_PREFIX}.tablesNeedAttention`}
          components={{
            statusLink: (
              <button
                type="button"
                className={`${classes.link} ${classes.linkButton}`}
                onClick={onViewTables}
              />
            )
          }}
        />
      }
      description={
        tableCountsOfConcern.restartRequired > 0 ? (
          <>
            <span className={classes.step}>
              <span>
                1.{' '}
                <Trans
                  i18nKey={`${TRANSLATION_KEY_PREFIX}.restartRequiredStep`}
                  components={{ bold: <b /> }}
                />
              </span>
              {restartButton}
            </span>
            <span className={classes.step}>
              <span>
                2.{' '}
                <Trans
                  i18nKey={`${TRANSLATION_KEY_PREFIX}.otherStatusesStep`}
                  components={{ docsLink: tableStatusDocsLink }}
                />
              </span>
            </span>
          </>
        ) : (
          <Trans
            i18nKey={`${TRANSLATION_KEY_PREFIX}.tableStatusDocs`}
            components={{ docsLink: tableStatusDocsLink }}
          />
        )
      }
    />
  );
};
