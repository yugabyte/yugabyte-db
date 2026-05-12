import { YBModal, YBModalProps } from '@app/redesign/components';
import { makeStyles, Link as MuiLink, Typography } from '@material-ui/core';
import { useTranslation } from 'react-i18next';
import { handleServerError } from '@app/utils/errorHandlingUtils';
import { AxiosError } from 'axios';

import ExternalLinkIcon from '@app/redesign/assets/approved/share-04.svg';
import { useFinalizeSoftwareUpgrade } from '@app/v2/api/universe/universe';
import { ApiPermissionMap } from '@app/redesign/features/rbac/ApiAndUserPermMapping';
import { hasNecessaryPerm } from '@app/redesign/features/rbac/common/RbacApiPermValidator';
import { RBAC_ERR_MSG_NO_PERM } from '@app/redesign/features/rbac/common/validator/ValidatorUtils';
import { YBATaskRespResponse } from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';
import { useRefreshUniverseTasksCache } from '@app/redesign/helpers/cacheUtils';
import { YBA_UNIVERSE_UPGRADE_EVALUATION_LINK } from './constants';

const useStyles = makeStyles((theme) => ({
  modalContainer: {
    display: 'flex',
    flexDirection: 'column',

    padding: theme.spacing(2, 2.5)
  },
  finalizeTextContainer: {
    display: 'flex',
    flexDirection: 'column',
    gap: theme.spacing(2)
  },
  evaluationLink: {
    display: 'flex',
    gap: theme.spacing(0.25),

    marginTop: theme.spacing(5)
  }
}));

interface DbUpgradeFinalizeModalProps {
  universeUuid: string;
  modalProps: YBModalProps;
}

const MODAL_NAME = 'DbUpgradeFinalizeModal';

export const DbUpgradeFinalizeModal = ({
  universeUuid,
  modalProps
}: DbUpgradeFinalizeModalProps) => {
  const classes = useStyles();
  const refreshUniverseDetailsCache = useRefreshUniverseTasksCache(universeUuid);
  const { t } = useTranslation('translation', {
    keyPrefix: 'universeActions.dbUpgrade.finalizeModal'
  });
  const finalizeUpgradeMutation = useFinalizeSoftwareUpgrade({
    mutation: {
      onSuccess: (_: YBATaskRespResponse, variables: { uniUUID: string }) => {
        refreshUniverseDetailsCache();
        modalProps.onClose();
      },
      onError: (error: Error | AxiosError) =>
        handleServerError(error, {
          customErrorLabel: t('toast.finalizeUpgradeFailedLabel')
        })
    }
  });

  const finalizeUpgrade = () => {
    finalizeUpgradeMutation.mutate({
      uniUUID: universeUuid,
      data: {}
    });
  };

  const hasFinalizePermission = hasNecessaryPerm({
    onResource: universeUuid,
    ...ApiPermissionMap.UPGRADE_UNIVERSE_FINALIZE
  });
  const isFormDisabled = finalizeUpgradeMutation.isLoading || !hasFinalizePermission;
  return (
    <YBModal
      title={t('modalTitle')}
      titleSeparator
      submitLabel={t('submitLabel')}
      cancelLabel={t('cancel', { keyPrefix: 'common' })}
      submitTestId={`${MODAL_NAME}-SubmitButton`}
      cancelTestId={`${MODAL_NAME}-CancelButton`}
      size="sm"
      dialogContentProps={{
        className: classes.modalContainer
      }}
      onSubmit={finalizeUpgrade}
      isSubmitting={finalizeUpgradeMutation.isLoading}
      buttonProps={{
        primary: {
          disabled: isFormDisabled
        }
      }}
      submitButtonTooltip={!hasFinalizePermission ? RBAC_ERR_MSG_NO_PERM : ''}
      {...modalProps}
    >
      <div className={classes.finalizeTextContainer}>
        <Typography variant="body2">{t('noRollBackAfterFinalize')}</Typography>
        <Typography variant="body2">{t('makeSureToEvaluatePerformance')}</Typography>
      </div>
      <MuiLink
        href={YBA_UNIVERSE_UPGRADE_EVALUATION_LINK}
        target="_blank"
        rel="noopener noreferrer"
        className={classes.evaluationLink}
        color="textPrimary"
        underline="always"
      >
        <Typography variant="body2">{t('evaluationLink')}</Typography>
        <ExternalLinkIcon />
      </MuiLink>
    </YBModal>
  );
};
