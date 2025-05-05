import { UniverseInfo, UniverseSpec } from '../../../../../v2/api/yugabyteDBAnywhereV2APIs.schemas';

export interface DatabaseSettingsProps {
  ysql?: UniverseSpec['ysql'];
  ycql?: UniverseSpec['ycql'];
  enableConnectionPooling?: boolean;
  enablePGCompatibitilty?: boolean;
  ysql_confirm_password?: string;
  ycql_confirm_password?: string;
}
