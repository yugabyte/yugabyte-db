package api.v2.mappers;

import api.v2.models.ContinuousBackup;
import api.v2.models.ContinuousBackupInfo;
import api.v2.models.ContinuousBackupSpec;
import api.v2.models.TimeUnitType;
import com.yugabyte.yw.models.ContinuousBackupConfig;
import java.time.Instant;
import java.time.OffsetDateTime;
import java.time.ZoneOffset;
import org.mapstruct.Mapper;
import org.mapstruct.factory.Mappers;

@Mapper(config = CentralConfig.class)
public interface ContinuousBackupMapper {
  final ContinuousBackupMapper INSTANCE = Mappers.getMapper(ContinuousBackupMapper.class);

  default ContinuousBackup toContinuousBackup(ContinuousBackupConfig cbConfig) {
    ContinuousBackup v2ContinuousBackup = new ContinuousBackup();
    ContinuousBackupInfo v2ContinuousBackupInfo = new ContinuousBackupInfo();
    ContinuousBackupSpec v2ContinuousBackupSpec = new ContinuousBackupSpec();
    // User provided spec
    v2ContinuousBackupSpec.setStorageConfigUuid(cbConfig.getStorageConfigUUID());
    v2ContinuousBackupSpec.setFrequency(cbConfig.getFrequency());
    v2ContinuousBackupSpec.setFrequencyTimeUnit(
        TimeUnitType.valueOf(cbConfig.getFrequencyTimeUnit().name()));
    v2ContinuousBackupSpec.setBackupDir(cbConfig.getBackupDir());
    // System generated info
    v2ContinuousBackupInfo.setUuid(cbConfig.getUuid());
    v2ContinuousBackupInfo.setStorageLocation(cbConfig.getStorageLocation());
    Long lastBackup = cbConfig.getLastBackup();
    OffsetDateTime backupTime =
        (lastBackup == null || lastBackup == 0)
            ? null
            : Instant.ofEpochMilli(lastBackup).atOffset(ZoneOffset.UTC);
    v2ContinuousBackupInfo.setLastBackup(backupTime);
    v2ContinuousBackup.setInfo(v2ContinuousBackupInfo);
    v2ContinuousBackup.setSpec(v2ContinuousBackupSpec);
    return v2ContinuousBackup;
  }
}
