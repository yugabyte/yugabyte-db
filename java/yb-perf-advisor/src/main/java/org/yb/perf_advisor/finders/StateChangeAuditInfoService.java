package org.yb.perf_advisor.finders;

import io.ebean.EbeanServer;
import org.yb.perf_advisor.models.PerformanceRecommendation;
import org.yb.perf_advisor.models.StateChangeAuditInfo;

import java.time.OffsetDateTime;
import java.util.UUID;

public class StateChangeAuditInfoService {

  EbeanServer ebeanServer;

  public StateChangeAuditInfoService(EbeanServer ebeanServer) {
    this.ebeanServer = ebeanServer;
  }

  public StateChangeAuditInfo create(StateChangeAuditInfo stateChangeAuditInfo){
    ebeanServer.save(stateChangeAuditInfo);
    return stateChangeAuditInfo;
  }

  public StateChangeAuditInfo create(PerformanceRecommendation performanceRecommendation, String fieldName, String previousValue, String updatedValue, UUID userId){
    StateChangeAuditInfo stateChangeAuditInfo = new StateChangeAuditInfo();
    stateChangeAuditInfo.performanceRecommendation=performanceRecommendation;
    stateChangeAuditInfo.fieldName=fieldName;
    stateChangeAuditInfo.previousValue=previousValue;
    stateChangeAuditInfo.updatedValue=updatedValue;
    stateChangeAuditInfo.userId=userId;
    stateChangeAuditInfo.timestamp= OffsetDateTime.now();
    return create(stateChangeAuditInfo);
  }

  public StateChangeAuditInfo get(UUID id){
    return ebeanServer.find(StateChangeAuditInfo.class).where().eq("id", id).findOne();
  }
}
