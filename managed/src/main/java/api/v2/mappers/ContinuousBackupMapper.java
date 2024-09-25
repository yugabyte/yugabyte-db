package api.v2.mappers;

import api.v2.models.ContinuousBackup;
import api.v2.models.ContinuousBackupInfo;
import api.v2.models.ContinuousBackupSpec;
import api.v2.models.TimeUnitType;
import com.yugabyte.yw.models.ContinuousBackupConfig;
import java.time.OffsetDateTime;
import org.mapstruct.Mapper;
import org.mapstruct.factory.Mappers;

@Mapper(config = CentralConfig.class)
public interface ContinuousBackupMapper {
  final ContinuousBackupMapper INSTANCE = Mappers.getMapper(ContinuousBackupMapper.class);

  default ContinuousBackup toContinuousBackup(ContinuousBackupConfig cbConfig) {
    ContinuousBackup v2ContinuousBackup = new ContinuousBackup();
    ContinuousBackupInfo v2ContinuousBackupInfo = new ContinuousBackupInfo();
    ContinuousBackupSpec v2ContinuousBackupSpec = new ContinuousBackupSpec();
    v2ContinuousBackupInfo.setUuid(cbConfig.getUuid());
    v2ContinuousBackupSpec.setStorageConfigUuid(cbConfig.getStorageConfigUUID());
    v2ContinuousBackupSpec.setFrequency(cbConfig.getFrequency());
    v2ContinuousBackupSpec.setFrequencyTimeUnit(
        TimeUnitType.valueOf(cbConfig.getFrequencyTimeUnit().name()));
    // TODO: compute from actual cbConfig
    v2ContinuousBackupInfo.setStorageLocation("s3://backup_bucket/YBA.1.2.3.4/");
    v2ContinuousBackupInfo.setLastBackup(OffsetDateTime.parse("2024-08-19T10:30:45-04:00"));
    v2ContinuousBackup.setInfo(v2ContinuousBackupInfo);
    v2ContinuousBackup.setSpec(v2ContinuousBackupSpec);
    return v2ContinuousBackup;
  }
}
