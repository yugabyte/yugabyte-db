import { FC, useEffect } from "react";
import { useTranslation } from "react-i18next";
import { useFormContext } from "react-hook-form";
import { Box } from "@material-ui/core";
import { YBLabel, YBToggleField, YBTooltip } from "@app/redesign/components";
import { CloudType, UniverseFormData } from "../../../utils/dto";
import { PROVIDER_FIELD, ROOT_CA_CLIENT_CA_SAME_FIELD } from "../../../utils/constants";
import InfoMessageIcon from '../../../../../../assets/info-message.svg?img';

interface ClientCaAndRootCASameFieldProps {
    disabled?: boolean;
    isCreateMode: boolean;

}
export const ClientCaAndRootCASameField: FC<ClientCaAndRootCASameFieldProps> = ({disabled}) => {
    const { control, watch, setValue } = useFormContext<UniverseFormData>();
    const { t } = useTranslation();
    const provider = watch(PROVIDER_FIELD);

    useEffect(()=> {
        if(provider?.code === CloudType.kubernetes) {
            setValue(ROOT_CA_CLIENT_CA_SAME_FIELD, true);
        }
    }, [provider]);

    return (
        <Box display="flex" width="100%" data-testid="ClientToNodeTLSField-Container">
            <YBToggleField
                name={ROOT_CA_CLIENT_CA_SAME_FIELD}
                inputProps={{
                    'data-testid': 'ClientToNodeTLSField-Toggle'
                }}
                control={control}
                disabled={provider?.code === CloudType.kubernetes || disabled}
            />
            <Box flex={1} alignSelf="center">
                <YBLabel dataTestId="ClientToNodeTLSField-Label" width="fit-content">
                    {t('universeForm.securityConfig.encryptionSettings.clientCAAndRootCASame')}
                    &nbsp;
                    <YBTooltip title={"Use the same certificate for “Node to Node” and “Client to Node” encryption"}>
                        <img alt="Info" src={InfoMessageIcon} />
                    </YBTooltip>
                </YBLabel>
            </Box>
        </Box>
    );
};
