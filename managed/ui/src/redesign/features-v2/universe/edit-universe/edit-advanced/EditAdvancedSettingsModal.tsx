import { useMemo } from 'react';
import { useTranslation } from 'react-i18next';
import { toast } from 'react-toastify';
import { FormProvider, useForm } from 'react-hook-form';
import { yupResolver } from '@hookform/resolvers/yup';
import { yba } from '@yugabyte-ui-library/core';
import { EnableProxyServer } from '../../create-universe/fields';
import { ProxyAdvancedProps } from '../../create-universe/steps/advanced-settings/dtos';
import {
  convertProxySettingsToFormValues,
  getClusterByType,
  useEditUniverseContext
} from '../EditUniverseUtils';
import { useUpdateProxyConfig } from '@app/v2/api/universe/universe';
import { useEditUniverseTaskHandler } from '../hooks/useEditUniverseTaskHandler';
import { createErrorMessage } from '../../../../../utils/ObjectUtils';
import { ProxySettingsValidationSchema } from '../../create-universe/steps/advanced-settings/ProxySettingsValidationSchema';
import { ClusterSpecClusterType } from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';

const { YBModal } = yba;

interface EditAdvancedSettingsModalProps {
  visible: boolean;
  onClose: () => void;
}

export const EditAdvancedSettingsModal = ({ visible, onClose }: EditAdvancedSettingsModalProps) => {
  const { t } = useTranslation();
  const { universeData } = useEditUniverseContext();
  const updateProxy = useUpdateProxyConfig();
  const universeUUID = universeData?.info?.universe_uuid;
  const primaryCluster = getClusterByType(universeData!, ClusterSpecClusterType.PRIMARY);
  const handleEditUniverseSuccess = useEditUniverseTaskHandler(universeUUID);

  const defaultValues = convertProxySettingsToFormValues(
    primaryCluster?.networking_spec?.proxy_config ?? {}
  );

  const validationSchema = useMemo(() => ProxySettingsValidationSchema(t), [t]);
  const methods = useForm<ProxyAdvancedProps>({
    defaultValues,
    resolver: yupResolver(validationSchema),
    mode: 'onSubmit',
    reValidateMode: 'onChange',
    criteriaMode: 'all'
  });

  const { handleSubmit } = methods;
  const handleFormSubmit = handleSubmit(async (values) => {
    if (!universeUUID || !primaryCluster?.uuid) {
      toast.error(t('unableToApplyChanges'));
      return;
    }

    updateProxy.mutate(
      {
        uniUUID: universeUUID,
        data: {
          clusters: [
            {
              uuid: primaryCluster.uuid,
              networking_spec: {
                ...(values?.enableProxyServer
                  ? {
                      proxy_config: {
                        http_proxy: !!values?.webProxy
                          ? `${values?.webProxyServer}:${values?.webProxyPort}`
                          : '',
                        https_proxy: !!values?.secureWebProxy
                          ? `${values?.secureWebProxyServer}:${values?.secureWebProxyPort}`
                          : '',
                        ...(!!values?.byPassProxyList
                          ? { no_proxy_list: values?.byPassProxyListValues ?? [] }
                          : {})
                      }
                    }
                  : {})
              }
            }
          ]
        }
      },
      {
        onSuccess: (response) => {
          handleEditUniverseSuccess(response.task_uuid);
          onClose();
        },
        onError: (error: unknown) => {
          toast.error(createErrorMessage(error));
        }
      }
    );
  });

  return (
    <YBModal
      open={visible}
      onClose={onClose}
      title={t('editProxyConfiguration')}
      submitLabel={t('save', { keyPrefix: 'common' })}
      cancelLabel={t('cancel', { keyPrefix: 'common' })}
      titleSeparator
      size="md"
      dialogContentProps={{ sx: { padding: '16px !important' } }}
      overrideHeight={'fit-content'}
      overrideWidth={'fit-content'}
      onSubmit={handleFormSubmit}
    >
      <FormProvider {...methods}>
        <EnableProxyServer disabled={false} />
      </FormProvider>
    </YBModal>
  );
};
