package com.yugabyte.troubleshoot.ts.yba.client;

import com.yugabyte.troubleshoot.ts.models.UniverseDetails;
import com.yugabyte.troubleshoot.ts.models.UniverseMetadata;
import com.yugabyte.troubleshoot.ts.yba.models.RunQueryFormData;
import com.yugabyte.troubleshoot.ts.yba.models.RunQueryResult;
import org.apache.commons.lang3.StringUtils;
import org.springframework.http.HttpEntity;
import org.springframework.http.HttpHeaders;
import org.springframework.http.HttpMethod;
import org.springframework.http.ResponseEntity;
import org.springframework.stereotype.Component;
import org.springframework.util.MultiValueMap;
import org.springframework.web.client.RestTemplate;
import org.springframework.web.util.UriComponentsBuilder;

@Component
public class YBAClient {

  private final RestTemplate ybaClientTemplate;

  public YBAClient(RestTemplate ybaClientTemplate) {
    this.ybaClientTemplate = ybaClientTemplate;
  }

  public UniverseDetails getUniverseDetails(UniverseMetadata universe) {
    HttpEntity<Void> request = emptyRequest(universe);
    ResponseEntity<UniverseDetails> response =
        ybaClientTemplate.exchange(
            buildUniverseUri(universe), HttpMethod.GET, request, UniverseDetails.class);
    return response.getBody();
  }

  public RunQueryResult runSqlQuery(
      UniverseMetadata universe, String database, String query, String nodeName) {
    RunQueryFormData formData = new RunQueryFormData();
    formData.setQuery(query);
    formData.setDbName(database);
    formData.setNodeName(nodeName);
    HttpEntity<RunQueryFormData> entity = wrapRequest(universe, formData);
    return ybaClientTemplate.postForObject(
        buildUniverseUri(universe, "run_query"), entity, RunQueryResult.class);
  }

  private String buildUniverseUri(UniverseMetadata universe) {
    return buildUniverseUri(universe, null);
  }

  private String buildUniverseUri(UniverseMetadata universe, String path) {
    return buildUniverseUri(universe, path, null);
  }

  private HttpEntity<Void> emptyRequest(UniverseMetadata metadata) {
    HttpHeaders headers = new HttpHeaders();
    headers.add("X-AUTH-YW-API-TOKEN", metadata.getApiToken());
    return new HttpEntity<Void>(headers);
  }

  private <T> HttpEntity<T> wrapRequest(UniverseMetadata metadata, T body) {
    HttpHeaders headers = new HttpHeaders();
    headers.add("X-AUTH-YW-API-TOKEN", metadata.getApiToken());
    return new HttpEntity<T>(body, headers);
  }

  private String buildUniverseUri(
      UniverseMetadata universe, String path, MultiValueMap<String, String> params) {
    UriComponentsBuilder uriComponentsBuilder =
        UriComponentsBuilder.fromUriString(universe.getPlatformUrl())
            .path("/api/v1")
            .pathSegment("customers")
            .pathSegment(universe.getCustomerId().toString())
            .pathSegment("universes")
            .pathSegment(universe.getId().toString());
    if (StringUtils.isNotEmpty(path)) {
      uriComponentsBuilder.path(path);
    }
    if (params != null) {
      uriComponentsBuilder.queryParams(params);
    }
    return uriComponentsBuilder.build().toString();
  }
}
