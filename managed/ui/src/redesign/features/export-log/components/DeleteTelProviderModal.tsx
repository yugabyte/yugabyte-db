import { FC } from 'react';
import { toast } from 'react-toastify';
import { useMutation } from 'react-query';
import { useTranslation, Trans } from 'react-i18next';
import { YBModal } from '../../../components';
import { api } from '../../../utils/api';
import { createErrorMessage } from '../../universe/universe-form/utils/helpers';
//RBAC
import { hasNecessaryPerm } from '../../rbac/common/RbacApiPermValidator';
import { ApiPermissionMap } from '../../rbac/ApiAndUserPermMapping';
import { RBAC_ERR_MSG_NO_PERM } from '../../rbac/common/validator/ValidatorUtils';

export interface TelemetryProviderMin {
  uuid: string;
  name: string;
}

interface DeleteTelProviderProps {
  telemetryProviderProps: TelemetryProviderMin;
  open: boolean;
  onClose: () => void;
}

export const DeleteTelProviderModal: FC<DeleteTelProviderProps> = ({
  telemetryProviderProps,
  open,
  onClose
}) => {
  const { t } = useTranslation();

  const deleteTelemetryProvider = useMutation(
    (providerUUID: string) => {
      return api.deleteTelemetryProvider(providerUUID);
    },
    {
      onSuccess: () => {
        toast.success(t('exportAuditLog.deleteConfirmMsg', { name: telemetryProviderProps.name }));
        onClose();
      },
      onError: (error: any) => {
        toast.error(createErrorMessage(error));
      }
    }
  );

  const onSubmit = async () => {
    try {
      await deleteTelemetryProvider.mutate(telemetryProviderProps.uuid);
    } catch (e) {
      console.error(e);
    }
  };

  const canDeleteProvider = hasNecessaryPerm(ApiPermissionMap.DELETE_TELEMETRY_PROVIDER_BY_ID);

  return (
    <YBModal
      title={t('exportAuditLog.deleteModalTitle')}
      submitLabel={t('exportAuditLog.deleteModalSubmitLabel')}
      cancelLabel={t('common.close')}
      open={open}
      size="sm"
      overrideHeight={'250px'}
      onSubmit={onSubmit}
      onClose={onClose}
      submitTestId="DeleteTelProviderModal-Submit"
      cancelTestId="DeleteTelProviderModal-Cancel"
      buttonProps={{
        primary: {
          disabled: !canDeleteProvider
        }
      }}
      submitButtonTooltip={!canDeleteProvider ? RBAC_ERR_MSG_NO_PERM : ''}
    >
      <Trans
        i18nKey={'exportAuditLog.deleteModalMsg'}
        values={{ name: telemetryProviderProps.name }}
      />
    </YBModal>
  );
};
