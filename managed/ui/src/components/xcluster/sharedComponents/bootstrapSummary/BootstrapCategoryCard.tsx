import { makeStyles, Typography } from '@material-ui/core';
import clsx from 'clsx';
import copy from 'copy-to-clipboard';
import { useState } from 'react';
import { Trans, useTranslation } from 'react-i18next';
import { YBTooltip } from '../../../../redesign/components';

import { BootstrapCategory, I18N_KEY_PREFIX_XCLUSTER_TERMS } from '../../constants';

import { CategoryNeedBootstrapResponse } from '../../XClusterTypes';

interface BootstrapCategoryCardProps {
  categoryNeedBootstrapResponse: CategoryNeedBootstrapResponse;
  isBootstrapPlanned: boolean;
  isDrInterface: boolean;
}

const useStyles = makeStyles((theme) => ({
  copyButton: {
    display: 'flex',
    justifyContent: 'center',
    alignItems: 'center',

    marginLeft: 'auto',
    minWidth: '32px',
    height: '32px',

    borderRadius: '8px'
  },
  copyAvailable: {
    '&:hover': {
      cursor: 'pointer',
      backgroundColor: theme.palette.ybacolors.backgroundGrayDark
    }
  },
  copySuccess: {
    color: theme.palette.success[500],
    backgroundColor: theme.palette.primary[200],
    border: `1px solid ${theme.palette.ybacolors.ybBorderGray}`
  },
  bootstrapCategoryCard: {
    display: 'flex',
    alignItems: 'center',
    gap: theme.spacing(1),

    width: 380,
    padding: theme.spacing(1),

    border: `1px solid ${theme.palette.ybacolors.ybGray}`,
    borderRadius: '8px'
  },
  warningIcon: {
    color: theme.palette.ybacolors.warning
  }
}));

const TRANSLATION_KEY_PREFIX = 'clusterDetail.xCluster.shared.bootstrapSummary.categoryCard';

export const BootstrapCategoryCard = ({
  categoryNeedBootstrapResponse,
  isBootstrapPlanned,
  isDrInterface
}: BootstrapCategoryCardProps) => {
  const [copied, setCopied] = useState<boolean>(false);
  const classes = useStyles();

  const { tables: tablesJson, tableCount, bootstrapCategory } = categoryNeedBootstrapResponse;

  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });
  const handleCopy = () => {
    copy(JSON.stringify(tablesJson));
    setCopied(true);
    setTimeout(() => {
      setCopied(false);
    }, 2000);
  };

  const copyButtonIcon = copied ? (
    <i className="fa fa-check" aria-hidden="true" />
  ) : (
    <i className="fa fa-clone" aria-hidden="true" />
  );

  const isBootstrapRequired = bootstrapCategory !== BootstrapCategory.NO_BOOTSTRAP_REQUIRED;
  const isDataPossiblyInconsistent = isBootstrapRequired && !isBootstrapPlanned;
  return (
    <div className={classes.bootstrapCategoryCard}>
      <Typography variant="body2">
        <Trans
          i18nKey={`${TRANSLATION_KEY_PREFIX}.${bootstrapCategory}.label`}
          components={{ bold: <b /> }}
          values={{
            count: tableCount,
            targetUniverseTerm: t(`target.${isDrInterface ? 'dr' : 'xClusterReplication'}`, {
              keyPrefix: I18N_KEY_PREFIX_XCLUSTER_TERMS
            })
          }}
        />
      </Typography>
      {isDataPossiblyInconsistent && (
        <YBTooltip title={t('inconsistentData')}>
          <i className={clsx('fa fa-warning', classes.warningIcon)} />
        </YBTooltip>
      )}
      <div
        className={clsx(classes.copyButton, copied ? classes.copySuccess : classes.copyAvailable)}
        onClick={handleCopy}
      >
        {copyButtonIcon}
      </div>
    </div>
  );
};
