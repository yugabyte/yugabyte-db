package com.yugabyte.troubleshoot.ts.service;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.google.common.collect.ImmutableSet;
import com.yugabyte.troubleshoot.ts.metric.client.PrometheusClient;
import com.yugabyte.troubleshoot.ts.metric.models.MetricRangeQuery;
import com.yugabyte.troubleshoot.ts.metric.models.MetricResponse;
import com.yugabyte.troubleshoot.ts.models.*;
import java.time.Duration;
import java.time.ZoneOffset;
import java.util.*;
import java.util.regex.Pattern;
import java.util.stream.Collectors;
import lombok.Builder;
import lombok.SneakyThrows;
import lombok.Value;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections4.CollectionUtils;
import org.apache.commons.lang3.StringUtils;
import org.apache.commons.lang3.tuple.Pair;
import org.springframework.stereotype.Service;

@Slf4j
@Service
public class MetricGraphService implements GraphSourceIF {

  // If we have any special filter pattern, then we need to use =~ instead
  // of = in our filter condition. Special patterns include *, |, $ or +.
  private static final Pattern SPECIAL_FILTER_PATTERN = Pattern.compile("[*|+$]");

  public static final String SYSTEM_PLATFORM_DB = "system_platform";

  private static final String CONTAINER_METRIC_PREFIX = "container";
  public static final String TABLE_ID = "table_id";
  public static final String TABLE_NAME = "table_name";
  public static final String NAMESPACE_NAME = "namespace_name";
  public static final String NAMESPACE_ID = "namespace_id";

  private static final Set<String> DATA_DISK_USAGE_METRICS =
      ImmutableSet.of(
          "disk_usage", "disk_used_size_total", "disk_capacity_size_total", "disk_usage_percent");

  private static final Set<String> DISK_USAGE_METRICS =
      ImmutableSet.<String>builder()
          .addAll(DATA_DISK_USAGE_METRICS)
          .add("disk_volume_usage_percent")
          .add("disk_volume_used")
          .add("disk_volume_capacity")
          .build();

  private final PrometheusClient prometheusClient;
  private final Map<String, MetricsGraphConfig> metricGraphConfigs;

  @SneakyThrows
  public MetricGraphService(ObjectMapper objectMapper, PrometheusClient prometheusClient) {
    this.metricGraphConfigs =
        GraphService.fillConfigMap(
            objectMapper, "graphs/metric_graphs.yml", MetricsGraphConfig.class);
    this.prometheusClient = prometheusClient;
  }

  @Override
  public boolean supportsGraph(String name) {
    return metricGraphConfigs.containsKey(name);
  }

  @Override
  public long minGraphStepSeconds(UniverseMetadata universeMetadata) {
    return universeMetadata.getMetricsScrapePeriodSec() * 3;
  }

  @Override
  public GraphResponse getGraph(
      UniverseMetadata universeMetadata, UniverseDetails universeDetails, GraphQuery query) {
    GraphResponse result = new GraphResponse();
    result.setSuccessful(true);
    String graphName = query.getName();
    GraphSettings settings = query.getSettings();
    MetricsGraphConfig config = metricGraphConfigs.get(graphName);
    result.setName(graphName);
    if (config == null) {
      throw new IllegalArgumentException("Metric graph " + query.getName() + " does not exists");
    } else {
      query = preProcessFilters(universeMetadata, universeDetails, query);
      Map<String, String> topKQueries = Collections.emptyMap();
      Map<String, String> aggregatedQueries = Collections.emptyMap();
      MetricQueryContext context =
          MetricQueryContext.builder()
              .graphConfig(config)
              .graphQuery(query)
              .queryRangeSecs(query.getStepSeconds())
              .additionalGroupBy(getAdditionalGroupBy(settings))
              .excludeFilters(getExcludeFilters(settings))
              .build();
      if (settings.getSplitMode() != GraphSettings.SplitMode.NONE) {
        try {
          topKQueries = getTopKQueries(query, config);
        } catch (Exception e) {
          log.error("Error while generating top K queries for " + graphName, e);
          result.setSuccessful(false);
          result.setErrorMessage("Error while generating top K queries: " + e.getMessage());
          return result;
        }
        if (settings.isReturnAggregatedValue()) {
          try {
            MetricQueryContext aggregatedContext =
                context.toBuilder().secondLevelAggregation(true).build();
            aggregatedQueries = getQueries(settings, aggregatedContext);
          } catch (Exception e) {
            log.error("Error while generating aggregated queries for " + graphName, e);
            result.setSuccessful(false);
            result.setErrorMessage("Error while generating aggregated queries: " + e.getMessage());
            return result;
          }
        }
      }

      Map<String, String> queries = getQueries(settings, context);
      result.setLayout(config.getLayout());
      List<GraphResponse.GraphData> data = new ArrayList<>();
      for (Map.Entry<String, String> e : queries.entrySet()) {
        String metric = e.getKey();
        String queryExpr = e.getValue();
        String topKQuery = topKQueries.get(metric);
        if (!StringUtils.isEmpty(topKQuery)) {
          queryExpr += " and " + topKQuery;
        }
        String aggregatedQuery = aggregatedQueries.get(metric);
        if (!StringUtils.isEmpty(aggregatedQuery)) {
          queryExpr = "(" + queryExpr + ") or " + aggregatedQuery;
        }
        MetricResponse queryResponse = getMetrics(universeMetadata, query, queryExpr);
        if (queryResponse == null) {
          result.setData(new ArrayList<>());
          return result;
        }
        if (queryResponse.getStatus() == MetricResponse.Status.ERROR) {
          result.setSuccessful(false);
          result.setErrorMessage(queryResponse.getError());
          return result;
        } else {
          data.addAll(getGraphData(queryResponse, metric, config, settings));
        }
      }
      result.setData(data);
    }

    return result;
  }

  private MetricResponse getMetrics(
      UniverseMetadata universeMetadata, GraphQuery query, String queryExpression) {
    MetricRangeQuery rangeQuery = new MetricRangeQuery();
    rangeQuery.setQuery(queryExpression);
    rangeQuery.setStart(query.getStart().atZone(ZoneOffset.UTC));
    rangeQuery.setEnd(query.getEnd().atZone(ZoneOffset.UTC));
    rangeQuery.setStep(Duration.ofSeconds(query.getStepSeconds()));
    return prometheusClient.queryRange(universeMetadata.getMetricsUrl(), rangeQuery);
  }

  private Map<String, String> getTopKQueries(GraphQuery query, MetricsGraphConfig config) {
    GraphSettings settings = query.getSettings();
    if (settings.getSplitMode() == GraphSettings.SplitMode.NONE) {
      return Collections.emptyMap();
    }
    long range = query.getStepSeconds();
    long end = query.getEnd().getEpochSecond();
    MetricQueryContext context =
        MetricQueryContext.builder()
            .graphConfig(config)
            .graphQuery(query)
            .topKQuery(true)
            .queryRangeSecs(range)
            .queryTimestampSec(end)
            .additionalGroupBy(getAdditionalGroupBy(settings))
            .excludeFilters(getExcludeFilters(settings))
            .build();
    return getQueries(settings, context);
  }

  private Map<String, String> getQueries(GraphSettings settings, MetricQueryContext context) {
    String metric = context.getGraphConfig().getMetric();
    if (metric == null) {
      throw new RuntimeException("Invalid MetricContext: metric attribute is required");
    }
    Map<String, String> output = new LinkedHashMap<>();
    // We allow for the metric to be a | separated list of metric names, case in which we will split
    // and execute the metric queries individually.
    // Note: contains takes actual chars, while split takes a regex, hence the escape \\ there.
    if (metric.contains("|")) {
      for (String m : metric.split("\\|")) {
        MetricsGraphConfig config = metricGraphConfigs.get(m);
        output.put(
            m, getSingleMetricQuery(settings, context.toBuilder().graphConfig(config).build()));
      }
    } else {
      output.put(metric, getSingleMetricQuery(settings, context));
    }
    return output;
  }

  public String getSingleMetricQuery(GraphSettings settings, MetricQueryContext context) {
    MetricsGraphConfig config = context.getGraphConfig();
    String query = getQuery(config.getMetric(), settings, context);
    if (context.isTopKQuery()) {
      String topGroupBy = StringUtils.EMPTY;
      if (config.getGroupBy() != null) {
        Set<String> topGroupByLabels =
            Arrays.stream(config.getGroupBy().split(","))
                .filter(StringUtils::isNotBlank)
                .collect(Collectors.toSet());
        // No need to group by additional labels for top K query
        topGroupByLabels.removeAll(context.getAdditionalGroupBy());
        if (CollectionUtils.isNotEmpty(topGroupByLabels)) {
          topGroupBy = " by (" + String.join(", ", topGroupByLabels) + ")";
        }
      }
      switch (settings.getSplitMode()) {
        case TOP:
          return "topk(" + settings.getSplitCount() + ", " + query + ")" + topGroupBy;
        case BOTTOM:
          return "bottomk(" + settings.getSplitCount() + ", " + query + ")" + topGroupBy;
        default:
          throw new IllegalArgumentException(
              "Unexpected split mode "
                  + settings.getSplitMode().name()
                  + " for top/bottom K query");
      }
    }
    if (context.isSecondLevelAggregation()) {
      String queryStr =
          settings.getAggregatedValueFunction().getAggregationFunction() + "(" + query + ")";
      if (config.getGroupBy() != null) {
        Set<String> additionalGroupBySet = getAdditionalGroupBy(settings);
        Set<String> groupBySet =
            new HashSet<>(
                Arrays.stream(config.getGroupBy().split(","))
                    // Drop labels used in the current metric split as want to aggregate across
                    // these.
                    .filter(
                        (label) ->
                            StringUtils.isNotBlank(label) && !additionalGroupBySet.contains(label))
                    .collect(Collectors.toSet()));
        if (!groupBySet.isEmpty()) {
          queryStr = String.format("%s by (%s)", queryStr, String.join(", ", groupBySet));
        }
      }
      return queryStr;
    }
    return query;
  }

  /**
   * This method construct the prometheus queryString based on the metric config if additional
   * filters are provided, it applies those filters as well. example query string: -
   * avg(collectd_cpu_percent{cpu="system"}) - rate(collectd_cpu_percent{cpu="system"}[30m]) -
   * avg(collectd_memory{memory=~"used|buffered|cached|free"}) by (memory) -
   * avg(collectd_memory{memory=~"used|buffered|cached|free"}) by (memory) /10
   *
   * @return a valid prometheus query string
   */
  public String getQuery(String metric, GraphSettings settings, MetricQueryContext context) {
    // Special case searches for .avg to convert into the respective ratio of
    // avg(irate(metric_sum)) / avg(irate(metric_count))
    if (metric.endsWith(".avg")) {
      String metricPrefix = metric.substring(0, metric.length() - 4);
      String sumQuery = getQuery(metricPrefix + "_sum", GraphSettings.DEFAULT, context);
      String countQuery = getQuery(metricPrefix + "_count", GraphSettings.DEFAULT, context);
      return context.isSecondLevelAggregation()
          // Filter out the `NaN` values from divide by 0 when performing second level aggregation.
          ? String.format("(%s) / (%s != 0)", sumQuery, countQuery, countQuery)
          : String.format("(%s) / (%s)", sumQuery, countQuery);
    } else if (metric.contains("/")) {
      String[] metricNames = metric.split("/");
      MetricsGraphConfig numerator = metricGraphConfigs.get(metricNames[0]);
      MetricsGraphConfig denominator = metricGraphConfigs.get(metricNames[1]);
      String numQuery =
          getQuery(
              numerator.getMetric(),
              GraphSettings.DEFAULT,
              context.toBuilder().graphConfig(numerator).build());
      String denomQuery =
          getQuery(
              denominator.getMetric(),
              GraphSettings.DEFAULT,
              context.toBuilder().graphConfig(denominator).build());
      return context.isSecondLevelAggregation()
          // Filter out the `NaN` values from divide by 0 when performing second level aggregation.
          ? String.format("((%s) / (%s != 0)) * 100", numQuery, denomQuery, denomQuery)
          : String.format("((%s) / (%s)) * 100", numQuery, denomQuery);
    }

    String queryStr;
    StringBuilder query = new StringBuilder();
    query.append(metric);

    MetricsGraphConfig config = context.getGraphConfig();
    // If we have additional filters, we add them
    Map<String, String> allFilters = new HashMap<>(config.getFilters());
    Map<String, String> allExcludeFilters = new HashMap<>(config.getExcludeFilters());
    if (!context.getGraphQuery().getFilters().isEmpty()) {
      allFilters.putAll(
          context.getGraphQuery().getFilters().entrySet().stream()
              .filter(e -> StringUtils.isNotEmpty(e.getKey().getMetricLabel()))
              .collect(
                  Collectors.toMap(
                      e -> e.getKey().getMetricLabel(),
                      e -> e.getValue().stream().sorted().collect(Collectors.joining("|")))));

      // The kubelet volume metrics only has the persistentvolumeclain field
      // as well as namespace. Adding any other field will cause the query to fail.
      if (metric.startsWith("kubelet_volume")) {
        allFilters.remove(GraphFilter.podName.getMetricLabel());
        allFilters.remove(GraphFilter.containerName.getMetricLabel());
      }
      // For all other metrics, it is safe to remove the filter if
      // it exists.
      else {
        allFilters.remove(GraphFilter.pvc.getMetricLabel());
      }
    }
    allExcludeFilters.putAll(context.getExcludeFilters());
    if (!allFilters.isEmpty() || !allExcludeFilters.isEmpty()) {
      query.append(filtersToString(allFilters, allExcludeFilters));
    }

    // Range is applicable only when we have functions
    // TODO: also need to add a check, since range is applicable for only certain functions
    if (config.getRange() != null && config.getFunction() != null) {
      query.append(String.format("[%ds]", context.getQueryRangeSecs())); // for ex: [60s]
    }
    if (context.getQueryTimestampSec() != null) {
      query.append(String.format("@%d", context.getQueryTimestampSec())); // for ex: @1609746000
    }

    queryStr = query.toString();

    if (config.getFunction() != null) {
      String[] functions = config.getFunction().split("\\|");
      /* We have added special way to represent multiple functions that we want to
      do, we pipe delimit those, but they follow an order.
      Scenario 1:
        function: rate|avg,
        query str: avg(rate(metric{memory="used"}[30m]))
      Scenario 2:
        function: rate
        query str: rate(metric{memory="used"}[30m]). */
      for (String functionName : functions) {
        if (functionName.startsWith("quantile_over_time")) {
          String percentile = functionName.split("[.]")[1];
          queryStr = String.format("quantile_over_time(0.%s, %s)", percentile, queryStr);
        } else if (functionName.startsWith("topk") || functionName.startsWith("bottomk")) {
          if (settings.getSplitMode() != GraphSettings.SplitMode.NONE) {
            // TopK/BottomK is requested explicitly. Just ignore default split.
            continue;
          }
          String fun = functionName.split("[.]")[0];
          String count = functionName.split("[.]")[1];
          queryStr = String.format(fun + "(%s, %s)", count, queryStr);
        } else {
          queryStr = String.format("%s(%s)", functionName, queryStr);
        }
      }
    }

    if (config.getGroupBy() != null || CollectionUtils.isNotEmpty(context.getAdditionalGroupBy())) {
      Set<String> groupBySet = new HashSet<>();
      if (config.getGroupBy() != null) {
        groupBySet.addAll(
            Arrays.stream(config.getGroupBy().split(","))
                .filter(StringUtils::isNotBlank)
                .collect(Collectors.toSet()));
      }
      groupBySet.addAll(context.getAdditionalGroupBy());
      groupBySet.removeAll(context.getRemoveGroupBy());
      queryStr = String.format("%s by (%s)", queryStr, String.join(", ", groupBySet));
    }
    if (config.getOperator() != null) {
      queryStr = String.format("%s %s", queryStr, config.getOperator());
    }
    return queryStr;
  }

  /**
   * filtersToString method converts a map to a string with quotes around the value. The reason we
   * have to do this way is because prometheus expects the json key to have no quote, and just value
   * should have double quotes.
   *
   * @param filters is map<String, String>
   * @return String representation of the map ex: {memory="used", extra="1"} {memory="used"}
   *     {type=~"iostat_write_count|iostat_read_count"}
   */
  private String filtersToString(Map<String, String> filters, Map<String, String> excludeFilters) {
    List<String> filtersList = new ArrayList<>();
    for (Map.Entry<String, String> filter : filters.entrySet()) {
      if (SPECIAL_FILTER_PATTERN.matcher(filter.getValue()).find()) {
        filtersList.add(filter.getKey() + "=~\"" + filter.getValue() + "\"");
      } else {
        filtersList.add(filter.getKey() + "=\"" + filter.getValue() + "\"");
      }
    }
    for (Map.Entry<String, String> excludeFilter : excludeFilters.entrySet()) {
      if (SPECIAL_FILTER_PATTERN.matcher(excludeFilter.getValue()).find()) {
        filtersList.add(excludeFilter.getKey() + "!~\"" + excludeFilter.getValue() + "\"");
      } else {
        filtersList.add(excludeFilter.getKey() + "!=\"" + excludeFilter.getValue() + "\"");
      }
    }
    return "{" + String.join(", ", filtersList) + "}";
  }

  private static Set<String> getAdditionalGroupBy(GraphSettings settings) {
    return switch (settings.getSplitType()) {
      case NODE -> ImmutableSet.of(GraphFilter.instanceName.getMetricLabel());
      case TABLE -> ImmutableSet.of(NAMESPACE_NAME, NAMESPACE_ID, TABLE_ID, TABLE_NAME);
      case NAMESPACE -> ImmutableSet.of(NAMESPACE_NAME, NAMESPACE_ID);
      default -> Collections.emptySet();
    };
  }

  private static Map<String, String> getExcludeFilters(GraphSettings settings) {
    return switch (settings.getSplitType()) {
      case TABLE, NAMESPACE -> Collections.singletonMap(NAMESPACE_NAME, SYSTEM_PLATFORM_DB);
      default -> Collections.emptyMap();
    };
  }

  private List<GraphResponse.GraphData> getGraphData(
      MetricResponse response,
      String metricName,
      MetricsGraphConfig config,
      GraphSettings settings) {
    List<GraphResponse.GraphData> metricGraphDataList = new ArrayList<>();

    GraphLayout layout = config.getLayout();
    // We should use instance name for aggregated graph in case it's grouped by instance.
    boolean useInstanceName =
        config.getGroupBy() != null
            && config.getGroupBy().equals(GraphFilter.instanceName.getMetricLabel())
            && settings.getSplitMode() == GraphSettings.SplitMode.NONE;
    for (final MetricResponse.Result result : response.getData().getResult()) {
      GraphResponse.GraphData metricGraphData = new GraphResponse.GraphData();
      Map<String, String> metricInfo = result.getMetric();

      metricGraphData.instanceName = metricInfo.remove(GraphFilter.instanceName.getMetricLabel());
      metricGraphData.tableId = metricInfo.remove(TABLE_ID);
      metricGraphData.tableName = metricInfo.remove(TABLE_NAME);
      metricGraphData.namespaceName = metricInfo.remove(NAMESPACE_NAME);
      metricGraphData.namespaceId = metricInfo.remove(NAMESPACE_ID);
      if (metricInfo.containsKey("node_prefix")) {
        metricGraphData.name = metricInfo.get("node_prefix");
      } else if (metricInfo.size() == 1) {
        // If we have a group_by clause, the group by name would be the only
        // key in the metrics data, fetch that and use that as the name
        String key = metricInfo.keySet().iterator().next();
        metricGraphData.name = metricInfo.get(key);
      } else if (metricInfo.isEmpty()) {
        if (useInstanceName && StringUtils.isNotBlank(metricGraphData.instanceName)) {
          // In case of aggregated metric query need to set name == instanceName for graphs,
          // which are grouped by instance name by default
          metricGraphData.name = metricGraphData.instanceName;
        } else {
          metricGraphData.name = metricName;
        }
      }

      if (metricInfo.size() <= 1) {
        if (layout.getYaxis() != null
            && layout.getYaxis().getAlias().containsKey(metricGraphData.name)) {
          metricGraphData.name = layout.getYaxis().getAlias().get(metricGraphData.name);
        }
      } else {
        metricGraphData.labels = new HashMap<>();
        // In case we want to use instance name - it's already set above
        // Otherwise - replace metric name with alias.
        if (layout.getYaxis() != null && !useInstanceName) {
          for (Map.Entry<String, String> entry : layout.getYaxis().getAlias().entrySet()) {
            boolean validLabels = false;
            for (String key : entry.getKey().split(",")) {
              validLabels = false;
              for (String labelValue : metricInfo.values()) {
                if (labelValue.equals(key)) {
                  validLabels = true;
                  break;
                }
              }
              if (!validLabels) {
                break;
              }
            }
            if (validLabels) {
              metricGraphData.name = entry.getValue();
            }
          }
        } else {
          metricGraphData.labels.putAll(metricInfo);
        }
      }
      if (result.getValues() != null) {
        for (final Pair<Double, Double> value : result.getValues()) {
          metricGraphData.x.add((long) (value.getKey() * 1000));
          metricGraphData.y.add(String.valueOf(value.getValue()));
        }
      } else if (result.getValue() != null) {
        metricGraphData.x.add((long) (result.getValue().getKey() * 1000));
        metricGraphData.y.add(String.valueOf(result.getValue().getValue()));
      }
      metricGraphData.type = "scatter";
      metricGraphDataList.add(metricGraphData);
    }
    return sortGraphData(metricGraphDataList, config);
  }

  private List<GraphResponse.GraphData> sortGraphData(
      List<GraphResponse.GraphData> graphData, MetricsGraphConfig configDefinition) {
    Map<String, Integer> nameOrderMap = new HashMap<>();
    if (configDefinition.getLayout().getYaxis() != null
        && configDefinition.getLayout().getYaxis().getAlias() != null) {
      int position = 1;
      for (String alias : configDefinition.getLayout().getYaxis().getAlias().values()) {
        nameOrderMap.put(alias, position++);
      }
    }
    return graphData.stream()
        .sorted(
            Comparator.comparing(
                data -> {
                  if (StringUtils.isEmpty(data.name)) {
                    return Integer.MAX_VALUE;
                  }
                  if (StringUtils.isEmpty(data.instanceName)
                      && StringUtils.isEmpty(data.namespaceName)) {
                    return Integer.MAX_VALUE;
                  }
                  Integer position = nameOrderMap.get(data.name);
                  if (position != null) {
                    return position;
                  }
                  return Integer.MAX_VALUE - 1;
                }))
        .collect(Collectors.toList());
  }

  private GraphQuery preProcessFilters(
      UniverseMetadata metadata, UniverseDetails universe, GraphQuery query) {
    // Given we have a limitation on not being able to rename the pod labels in
    // kubernetes cadvisor metrics, we try to see if the metric being queried is for
    // container or not, and use pod_name vs exported_instance accordingly.
    // Expect for container metrics, all the metrics would with node_prefix and exported_instance.
    boolean isContainerMetric = query.getName().startsWith(CONTAINER_METRIC_PREFIX);
    GraphFilter universeFilterLabel =
        isContainerMetric ? GraphFilter.namespace : GraphFilter.universeUuid;
    GraphFilter nodeFilterLabel =
        isContainerMetric ? GraphFilter.podName : GraphFilter.instanceName;

    List<UniverseDetails.UniverseDefinition.NodeDetails> nodesToFilter = new ArrayList<>();
    Map<GraphFilter, List<String>> filters =
        query.getFilters() != null ? query.getFilters() : new HashMap<>();
    if (filters.containsKey(GraphFilter.clusterUuid)
        || filters.containsKey(GraphFilter.regionCode)
        || filters.containsKey(GraphFilter.azCode)
        || filters.containsKey(GraphFilter.instanceName)
        || filters.containsKey(GraphFilter.instanceType)) {
      List<UUID> clusterUuids =
          filters.getOrDefault(GraphFilter.clusterUuid, Collections.emptyList()).stream()
              .map(UUID::fromString)
              .toList();
      List<String> regionCodes =
          filters.getOrDefault(GraphFilter.regionCode, Collections.emptyList());
      List<String> azCodes = filters.getOrDefault(GraphFilter.azCode, Collections.emptyList());
      List<String> instanceNames =
          filters.getOrDefault(GraphFilter.instanceName, Collections.emptyList());
      List<UniverseDetails.InstanceType> instanceTypes =
          filters.getOrDefault(GraphFilter.instanceType, Collections.emptyList()).stream()
              .map(type -> UniverseDetails.InstanceType.valueOf(type.toUpperCase()))
              .toList();
      // Need to get matching nodes
      nodesToFilter.addAll(
          universe.getUniverseDetails().getNodeDetailsSet().stream()
              .filter(
                  node -> {
                    if (CollectionUtils.isNotEmpty(instanceTypes)
                        && instanceTypes.contains(UniverseDetails.InstanceType.MASTER)
                        && !node.isMaster()) {
                      return false;
                    }
                    if (CollectionUtils.isNotEmpty(instanceTypes)
                        && instanceTypes.contains(UniverseDetails.InstanceType.TSERVER)
                        && !node.isTserver()) {
                      return false;
                    }
                    if (CollectionUtils.isNotEmpty(clusterUuids)
                        && !clusterUuids.contains(node.getPlacementUuid())) {
                      return false;
                    }
                    if (CollectionUtils.isNotEmpty(regionCodes)
                        && !regionCodes.contains(node.getCloudInfo().getRegion())) {
                      return false;
                    }
                    if (CollectionUtils.isNotEmpty(azCodes)
                        && !azCodes.contains(node.getCloudInfo().getRegion())) {
                      return false;
                    }
                    if (CollectionUtils.isNotEmpty(instanceNames)
                        && !instanceNames.contains(node.getNodeName())) {
                      return false;
                    }
                    return true;
                  })
              .toList());

      if (CollectionUtils.isEmpty(nodesToFilter)) {
        throw new RuntimeException(
            "No nodes found based on passed universe, "
                + "clusters, regions, availability zones and node names");
      }
    }
    // Check if it is a Kubernetes deployment.
    if (isContainerMetric) {
      if (CollectionUtils.isEmpty(nodesToFilter)) {
        nodesToFilter = new ArrayList<>(universe.getUniverseDetails().getNodeDetailsSet());
      }
      Set<String> podNames = new HashSet<>();
      Set<String> containerNames = new HashSet<>();
      Set<String> pvcNames = new HashSet<>();
      Set<String> namespaces = new HashSet<>();
      for (UniverseDetails.UniverseDefinition.NodeDetails node : nodesToFilter) {
        String podName = node.getK8sPodName();
        String namespace = node.getK8sNamespace();
        String containerName = podName.contains("yb-master") ? "yb-master" : "yb-tserver";
        String pvcName = String.format("(.*)-%s", podName);
        podNames.add(podName);
        containerNames.add(containerName);
        pvcNames.add(pvcName);
        namespaces.add(namespace);
      }
      filters.put(nodeFilterLabel, new ArrayList<>(podNames));
      filters.put(GraphFilter.containerName, new ArrayList<>(containerNames));
      filters.put(GraphFilter.pvc, new ArrayList<>(pvcNames));
      filters.put(universeFilterLabel, new ArrayList<>(namespaces));
    } else {
      if (CollectionUtils.isNotEmpty(nodesToFilter)) {
        List<String> nodeNames =
            nodesToFilter.stream()
                .map(UniverseDetails.UniverseDefinition.NodeDetails::getNodeName)
                .collect(Collectors.toList());
        filters.put(nodeFilterLabel, nodeNames);
      }

      if (DISK_USAGE_METRICS.contains(query.getName())) {
        if (DATA_DISK_USAGE_METRICS.contains(query.getName())) {
          filters.put(GraphFilter.mountPoint, metadata.getDataMountPoints());
        } else {
          List<String> allMountPoints = new ArrayList<>(metadata.getOtherMountPoints());
          allMountPoints.addAll(metadata.getDataMountPoints());
          filters.put(GraphFilter.mountPoint, allMountPoints);
        }
      }
    }
    query.setFilters(filters);
    return query;
  }

  @Value
  @Builder(toBuilder = true)
  public static class MetricQueryContext {
    MetricsGraphConfig graphConfig;
    GraphQuery graphQuery;
    // Set in case we need query to be wrapped with topk or bottomk function
    @Builder.Default boolean topKQuery = false;
    // Set in case we need additional level of aggregation
    @Builder.Default boolean secondLevelAggregation = false;
    // Filters, applied to each metric query
    @Builder.Default Map<String, String> excludeFilters = Collections.emptyMap();
    // Group by, applied to each metric query
    @Builder.Default Set<String> additionalGroupBy = Collections.emptySet();
    // Group by, which need to be removed from original metric group by list
    @Builder.Default Set<String> removeGroupBy = Collections.emptySet();

    // Period, used in range queries, eg. (metric{labels}[60s]).
    Long queryRangeSecs;
    // Timestamp, used in range queries, eg. (metric{labels} @ 1609746000).
    Long queryTimestampSec;
  }
}
