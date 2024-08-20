// Copyright (c) YugaByte, Inc.

package api.v2.mappers;

import api.v2.models.JobConfigSpec;
import api.v2.models.JobInstancePagedQuerySpec;
import api.v2.models.JobInstancePagedResp;
import api.v2.models.JobSchedule;
import api.v2.models.JobScheduleConfigSpec;
import api.v2.models.JobScheduleInfo;
import api.v2.models.JobSchedulePagedQuerySpec;
import api.v2.models.JobSchedulePagedResp;
import api.v2.models.JobScheduleSpec;
import api.v2.models.JobScheduleType;
import api.v2.models.JobScheduleUpdateSpec;
import com.fasterxml.jackson.core.type.TypeReference;
import com.yugabyte.yw.forms.JobScheduleUpdateForm;
import com.yugabyte.yw.models.helpers.schedule.ScheduleConfig.ScheduleType;
import com.yugabyte.yw.models.paging.JobInstancePagedQuery;
import com.yugabyte.yw.models.paging.JobInstancePagedResponse;
import com.yugabyte.yw.models.paging.JobSchedulePagedQuery;
import com.yugabyte.yw.models.paging.JobSchedulePagedResponse;
import java.time.OffsetDateTime;
import java.time.ZoneOffset;
import java.util.Date;
import java.util.Map;
import org.mapstruct.EnumMapping;
import org.mapstruct.Mapper;
import org.mapstruct.Mapping;
import org.mapstruct.MappingConstants;
import org.mapstruct.factory.Mappers;
import play.libs.Json;

@Mapper(config = CentralConfig.class)
public interface JobSchedulerMapper {
  final JobSchedulerMapper INSTANCE = Mappers.getMapper(JobSchedulerMapper.class);

  @Mapping(target = "needTotalCount", constant = "true")
  JobSchedulePagedQuery toJobSchedulePagedQuery(JobSchedulePagedQuerySpec pagedQuerySpec);

  @EnumMapping(
      nameTransformationStrategy = MappingConstants.PREFIX_TRANSFORMATION,
      configuration = "FIXED_")
  ScheduleType toScheduleType(JobScheduleType type);

  @EnumMapping(
      nameTransformationStrategy = MappingConstants.STRIP_PREFIX_TRANSFORMATION,
      configuration = "FIXED_")
  JobScheduleType toJobScheduleType(ScheduleType type);

  default com.yugabyte.yw.models.JobSchedule.SortBy toJobScheduleSortBy(
      JobSchedulePagedQuerySpec.SortByEnum sortByEnum) {
    return sortByEnum == null
        ? null
        : com.yugabyte.yw.models.JobSchedule.SortBy.valueOf(sortByEnum.toString());
  }

  default JobSchedulePagedQuerySpec.SortByEnum toJobScheduleSortByEnum(
      com.yugabyte.yw.models.JobSchedule.SortBy sortBy) {
    return JobSchedulePagedQuerySpec.SortByEnum.fromValue(sortBy.name());
  }

  default com.yugabyte.yw.models.JobInstance.SortBy toJobInstanceSortBy(
      JobInstancePagedQuerySpec.SortByEnum sortByEnum) {
    return sortByEnum == null
        ? null
        : com.yugabyte.yw.models.JobInstance.SortBy.valueOf(sortByEnum.toString());
  }

  default JobInstancePagedQuerySpec.SortByEnum toJobInstanceSortByEnum(
      com.yugabyte.yw.models.JobInstance.SortBy sortBy) {
    return sortBy == null ? null : JobInstancePagedQuerySpec.SortByEnum.fromValue(sortBy.name());
  }

  default JobConfigSpec toJobConfigSpec(com.yugabyte.yw.models.helpers.schedule.JobConfig config) {
    JobConfigSpec jobConfigSpec = new JobConfigSpec();
    jobConfigSpec.setClassname(config.getClass().getName());
    jobConfigSpec.setConfig(
        Json.mapper()
            .convertValue(Json.toJson(config), new TypeReference<Map<String, Object>>() {}));
    return jobConfigSpec;
  }

  default OffsetDateTime toOffsetDateTime(Date date) {
    return date == null ? null : date.toInstant().atOffset(ZoneOffset.UTC);
  }

  default JobSchedule toJobSchedule(com.yugabyte.yw.models.JobSchedule jobSchedule) {
    JobSchedule v2JobSchedule = new JobSchedule();
    JobScheduleSpec spec = new JobScheduleSpec();
    spec.setJobConfig(toJobConfigSpec(jobSchedule.getJobConfig()));
    spec.setScheduleConfig(toJobScheduleSpec(jobSchedule.getScheduleConfig()));
    v2JobSchedule.setSpec(spec);
    v2JobSchedule.setInfo(toJobScheduleInfo(jobSchedule));
    return v2JobSchedule;
  }

  JobScheduleInfo toJobScheduleInfo(com.yugabyte.yw.models.JobSchedule jobSchedule);

  JobScheduleConfigSpec toJobScheduleSpec(
      com.yugabyte.yw.models.helpers.schedule.ScheduleConfig scheduleConfig);

  JobSchedulePagedResp toJobSchedulePagedResp(JobSchedulePagedResponse response);

  JobScheduleUpdateForm toJobScheduleUpdateForm(JobScheduleUpdateSpec jobScheduleUpdateSpec);

  @Mapping(target = "needTotalCount", constant = "true")
  JobInstancePagedQuery toJobInstancePagedQuery(JobInstancePagedQuerySpec pagedQuerySpec);

  JobInstancePagedResp toJobInstancePagedResp(JobInstancePagedResponse response);
}
