// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.helpers.schedule;

import com.fasterxml.jackson.annotation.JsonCreator;
import com.fasterxml.jackson.annotation.JsonIgnore;
import com.fasterxml.jackson.core.JacksonException;
import com.fasterxml.jackson.core.JsonGenerator;
import com.fasterxml.jackson.core.JsonParser;
import com.fasterxml.jackson.databind.DeserializationContext;
import com.fasterxml.jackson.databind.DeserializationFeature;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.SerializationFeature;
import com.fasterxml.jackson.databind.SerializerProvider;
import com.fasterxml.jackson.databind.annotation.JsonDeserialize;
import com.fasterxml.jackson.databind.annotation.JsonSerialize;
import com.fasterxml.jackson.databind.deser.std.StdDeserializer;
import com.fasterxml.jackson.databind.ser.std.StdSerializer;
import com.google.inject.Injector;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.models.JobInstance;
import com.yugabyte.yw.models.JobSchedule;
import com.yugabyte.yw.models.helpers.schedule.ScheduleConfig.ScheduleType;
import com.yugabyte.yw.scheduler.JobScheduler;
import java.io.IOException;
import java.io.Serializable;
import java.time.Instant;
import java.time.temporal.ChronoUnit;
import java.util.Date;
import java.util.concurrent.CompletableFuture;
import lombok.Builder;
import lombok.Getter;
import lombok.Setter;

/**
 * Configuration of the job to be run. This is implemented for any job submitted to the job
 * scheduler.
 */
public interface JobConfig extends Serializable {
  /**
   * This is invoked when the job is triggered to get the runnable. Any business logic can be
   * implemented in the runnable.
   *
   * @param runtimeParams the runtime parameters.
   * @return the runnable job.
   */
  CompletableFuture<?> executeJob(RuntimeParams runtimeParams);

  /**
   * Calculate the next start time based for the job schedule.
   *
   * @param jobSchedule the current job schedule.
   * @param restart restart from the current time if true.
   * @return the next execution time.
   */
  @JsonIgnore
  default Date createNextStartTime(JobSchedule jobSchedule, boolean restart) {
    ScheduleConfig scheduleConfig = jobSchedule.getScheduleConfig();
    if (restart) {
      return Date.from(
          Instant.now().plus(scheduleConfig.getInterval().getSeconds(), ChronoUnit.SECONDS));
    }
    Date lastTime = null;
    if (scheduleConfig.getType() == ScheduleType.FIXED_RATE) {
      lastTime = jobSchedule.getLastStartTime();
    } else if (scheduleConfig.getType() == ScheduleType.FIXED_DELAY) {
      lastTime = jobSchedule.getLastEndTime();
    } else {
      String errMsg =
          String.format(
              "Unknown schedule type %s for job schedule %s",
              scheduleConfig.getType(), jobSchedule.getUuid());
      throw new IllegalArgumentException(errMsg);
    }
    if (lastTime == null) {
      return Date.from(
          Instant.now().plus(scheduleConfig.getInterval().getSeconds(), ChronoUnit.SECONDS));
    }
    Instant nextTime =
        lastTime.toInstant().plus(scheduleConfig.getInterval().getSeconds(), ChronoUnit.SECONDS);
    if (nextTime.isBefore(Instant.now())) {
      // Expired long back.
      return Date.from(Instant.now().plus(1, ChronoUnit.MINUTES));
    }
    return Date.from(nextTime);
  }

  @Builder
  @Getter
  public static class RuntimeParams {
    private final Injector injector;
    private final RuntimeConfGetter confGetter;
    private final JobScheduler jobScheduler;
    private final JobSchedule jobSchedule;
    private final JobInstance jobInstance;
  }

  @Getter
  @Setter
  @JsonDeserialize(using = JobConfigWrapperDeserializer.class)
  @JsonSerialize(using = JobConfigWrapperSerializer.class)
  public static class JobConfigWrapper {
    private JobConfig config;

    @JsonCreator
    public JobConfigWrapper(JobConfig config) {
      this.config = config;
    }
  }

  @SuppressWarnings("serial")
  public static class JobConfigWrapperDeserializer extends StdDeserializer<JobConfigWrapper> {
    public JobConfigWrapperDeserializer() {
      super(JobConfigWrapper.class);
    }

    @Override
    public JobConfigWrapper deserialize(JsonParser p, DeserializationContext ctxt)
        throws IOException, JacksonException {
      JsonNode node = p.getCodec().readTree(p);
      JsonNode classNode = node.get("classname");
      if (classNode == null || classNode.isNull()) {
        throw new IllegalArgumentException("Class name is not found");
      }
      JsonNode configNode = node.get("config");
      if (configNode == null || configNode.isNull()) {
        throw new IllegalArgumentException("Config is not found");
      }
      String classname = classNode.asText();
      try {
        @SuppressWarnings("unchecked")
        Class<JobConfig> clazz = (Class<JobConfig>) Class.forName(classname);
        ObjectMapper mapper = ((ObjectMapper) p.getCodec()).copy();
        mapper.configure(DeserializationFeature.FAIL_ON_UNKNOWN_PROPERTIES, false);
        return new JobConfigWrapper(mapper.treeToValue(configNode, clazz));
      } catch (ClassNotFoundException e) {
        throw new IllegalArgumentException("Cannot load class " + classname, e);
      }
    }
  }

  @SuppressWarnings("serial")
  public static class JobConfigWrapperSerializer extends StdSerializer<JobConfigWrapper> {
    protected JobConfigWrapperSerializer() {
      super(JobConfigWrapper.class);
    }

    @Override
    public void serialize(JobConfigWrapper value, JsonGenerator gen, SerializerProvider provider)
        throws IOException {
      ObjectMapper mapper = ((ObjectMapper) gen.getCodec()).copy();
      mapper.configure(SerializationFeature.FAIL_ON_EMPTY_BEANS, false);
      gen.writeStartObject();
      gen.writeStringField("classname", value.getConfig().getClass().getName());
      gen.writeFieldName("config");
      gen.writeRawValue(mapper.writeValueAsString(value.getConfig()));
      gen.writeEndObject();
    }
  }
}
