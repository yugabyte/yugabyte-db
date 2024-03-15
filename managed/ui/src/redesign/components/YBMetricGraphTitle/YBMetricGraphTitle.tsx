import { makeStyles, Typography } from '@material-ui/core';
import { useTranslation } from 'react-i18next';

import { getPrometheusUrls } from '../../../components/metrics/utils';
import { YBTooltip } from '../YBTooltip/YBTooltip';
import { ReactComponent as PrometheusIcon } from '../../../redesign/assets/prometheus-icon.svg';

import { useAnchorStyles, useIconStyles } from '../../styles/styles';

interface YBMetricGraphTitleProps {
  title: string;

  metricsLinkUseBrowserFqdn?: boolean;
  directUrls?: string[];
}

const useStyles = makeStyles((theme) => ({
  chartTitle: {
    display: 'flex',
    gap: theme.spacing(1),
    alignItems: 'center'
  }
}));

const TRANSLATION_KEY_PREFIX = 'metric';

/**
 * Currently does not handle linking multiple prometheusl urls.
 */
export const YBMetricGraphTitle = ({
  title,
  metricsLinkUseBrowserFqdn,
  directUrls = []
}: YBMetricGraphTitleProps) => {
  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });
  const classes = useStyles();
  const anchorClasses = useAnchorStyles();
  const iconClasses = useIconStyles();

  return (
    <div className={classes.chartTitle}>
      <Typography variant="h4">{title}</Typography>
      {directUrls.length ? (
        <YBTooltip title={<Typography variant="body2">{t('tooltip.viewInPrometheus')}</Typography>}>
          <a
            target="_blank"
            rel="noopener noreferrer"
            className={anchorClasses.iconAnchor}
            href={
              // We haven't provided metric as a `|` seperated list, thus we
              // will only get a single prometheus URL here.
              getPrometheusUrls(directUrls, !!metricsLinkUseBrowserFqdn)[0]
            }
          >
            <PrometheusIcon className={iconClasses.interactiveIcon} />
          </a>
        </YBTooltip>
      ) : null}
    </div>
  );
};
