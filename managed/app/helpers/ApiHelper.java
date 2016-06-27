// Copyright (c) YugaByte, Inc.

package helpers;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import play.libs.ws.WSClient;
import play.libs.ws.WSResponse;

import java.util.concurrent.CompletionStage;
import java.util.concurrent.ExecutionException;

/**
 * Helper class API specific stuff
 */

@Singleton
public class ApiHelper {

	@Inject
	WSClient wsClient;

	public JsonNode postRequest(String url, JsonNode data)  {
		CompletionStage<JsonNode> jsonPromise = wsClient.url(url)
			.post(data)
			.thenApply(WSResponse::asJson);

		try {
			return jsonPromise.toCompletableFuture().get();
		} catch (InterruptedException e) {
			return ApiResponse.errorJSON(e.getMessage());
		} catch (ExecutionException e) {
			return ApiResponse.errorJSON(e.getMessage());
		}
	}

}
