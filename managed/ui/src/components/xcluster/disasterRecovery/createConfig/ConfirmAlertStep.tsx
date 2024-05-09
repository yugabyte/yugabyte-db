import { Box, makeStyles, Typography, useTheme } from '@material-ui/core';
import { Trans, useTranslation } from 'react-i18next';
import { useQuery } from 'react-query';
import { Link } from 'react-router';

import { getAlertConfigurations } from '../../../../actions/universe';
import { alertConfigQueryKey } from '../../../../redesign/helpers/api';
import { formatLagMetric } from '../../../../utils/Formatters';
import { PollingIntervalMs } from '../../constants';
import { getStrictestReplicationLagAlertThreshold } from '../../ReplicationUtils';

import {
  AlertTemplate,
  IAlertConfiguration as AlertConfiguration
} from '../../../../redesign/features/alerts/TemplateComposer/ICustomVariables';
import { Universe } from '../../../../redesign/helpers/dtos';

interface ConfirmAlertStepProps {
  isFormDisabled: boolean;
  sourceUniverse: Universe;
}

const useStyles = makeStyles((theme) => ({
  stepContainer: {
    '& ol': {
      paddingLeft: theme.spacing(2),
      listStylePosition: 'outside',
      '& li::marker': {
        fontWeight: 'bold'
      }
    }
  },
  instruction: {
    marginBottom: theme.spacing(4)
  },
  fieldLabel: {
    marginBottom: theme.spacing(1)
  },
  link: {
    textDecoration: 'underline'
  }
}));

const TRANSLATION_KEY_PREFIX =
  'clusterDetail.disasterRecovery.config.createModal.step.confirmAlert';

export const ConfirmAlertStep = ({ sourceUniverse }: ConfirmAlertStepProps) => {
  const theme = useTheme();
  const classes = useStyles();
  const { t } = useTranslation('translation', {
    keyPrefix: TRANSLATION_KEY_PREFIX
  });

  const alertConfigFilter = {
    template: AlertTemplate.REPLICATION_LAG,
    targetUuid: sourceUniverse.universeUUID
  };
  const alertConfigQuery = useQuery<AlertConfiguration[]>(
    alertConfigQueryKey.list(alertConfigFilter),
    () => getAlertConfigurations(alertConfigFilter),
    { refetchInterval: PollingIntervalMs.ALERT_CONFIGURATION }
  );

  /**
   * The existing replicaiton lag alert config with the lowest alert threshold.
   */
  const strictestReplicationLagAlertConfig = getStrictestReplicationLagAlertThreshold(
    alertConfigQuery.data
  );

  return (
    <div className={classes.stepContainer}>
      <ol start={4}>
        <li>
          <Typography variant="body1" className={classes.instruction}>
            {t('instruction')}
          </Typography>
          {alertConfigQuery.isLoading ? (
            <i className="fa fa-spinner fa-spin yb-spinner" />
          ) : alertConfigQuery.data?.length ? (
            <Box display="flex" flexDirection="column" gridGap={theme.spacing(2)}>
              <Box display="flex" gridGap={theme.spacing(1)}>
                <Typography variant="body2">
                  <Trans
                    i18nKey={`${TRANSLATION_KEY_PREFIX}.currentLowestReplicationLagLabel`}
                    values={{ sourceUniverseName: sourceUniverse.name }}
                  />
                </Typography>
                <Typography variant="body2">
                  {formatLagMetric(strictestReplicationLagAlertConfig)}
                </Typography>
              </Box>
              <Typography variant="body2">
                <Trans
                  i18nKey={`${TRANSLATION_KEY_PREFIX}.editReplicationLagAlertPrompt`}
                  components={{
                    manageAlertConfigLink: (
                      <Link
                        to={'/admin/alertConfig'}
                        target="_blank"
                        rel="noopener noreferrer"
                        className={classes.link}
                      />
                    )
                  }}
                />
              </Typography>
            </Box>
          ) : (
            <Typography variant="body2">
              <Trans
                i18nKey={`${TRANSLATION_KEY_PREFIX}.setUpReplicationLagAlertPrompt`}
                components={{
                  manageAlertConfigLink: (
                    <Link
                      to={'/admin/alertConfig'}
                      target="_blank"
                      rel="noopener noreferrer"
                      className={classes.link}
                    />
                  ),
                  paragraph: <p />
                }}
              />
            </Typography>
          )}
          <Box marginTop={6}>
            <Typography variant="body2">{t('infoText')}</Typography>
          </Box>
        </li>
      </ol>
    </div>
  );
};
