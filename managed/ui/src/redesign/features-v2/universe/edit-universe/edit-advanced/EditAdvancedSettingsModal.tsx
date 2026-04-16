import { useEffect } from 'react';
import { FormProvider, useForm } from 'react-hook-form';
import { useTranslation } from 'react-i18next';
import { mui, yba } from '@yugabyte-ui-library/core';
import { ProxyAdvancedProps } from '../../create-universe/steps/advanced-settings/dtos';
import { EnableProxyServer } from '../../create-universe/fields';
import {
  convertProxySettingsToFormValues,
  getClusterByType,
  useEditUniverseContext
} from '../EditUniverseUtils';
import { ClusterSpecClusterType } from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';

const { YBModal } = yba;
const { styled, Box, boxClasses } = mui;

interface EditAdvancedSettingsModalProps {
  visible: boolean;
  onClose: () => void;
}

const ModalContent = styled(Box)(({ theme }) => ({
  [`.yb-${boxClasses.root}`]: {
    width: '100%'
  }
}));
export const EditAdvancedSettingsModal = ({ visible, onClose }: EditAdvancedSettingsModalProps) => {
  const { t } = useTranslation('translation', { keyPrefix: 'editUniverse.advanced' });
  const { universeData } = useEditUniverseContext();
  const primaryCluster = getClusterByType(universeData!, ClusterSpecClusterType.PRIMARY);
  const defaultValues = convertProxySettingsToFormValues(
    primaryCluster?.networking_spec?.proxy_config ?? {}
  );
  const methods = useForm<ProxyAdvancedProps>({
    defaultValues
  });

  useEffect(() => {
    methods.reset(defaultValues);
  }, [visible]);

  if (!visible) return null;

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
      onSubmit={() => {}}
    >
      <FormProvider {...methods}>
        <ModalContent>
          <EnableProxyServer disabled={false} />
        </ModalContent>
      </FormProvider>
    </YBModal>
  );
};
