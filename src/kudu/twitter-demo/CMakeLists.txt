# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.

find_library(LIBOAUTH_LIBRARY NAMES oauth)
if(NOT LIBOAUTH_LIBRARY)
  message(WARNING "liboauth not found on system. Skipping twitter demo")
else()

  add_library(twitter_demo
    oauth.cc
    parser.cc
    insert_consumer.cc
    twitter_streamer.cc)

  target_link_libraries(twitter_demo
    gutil
    kudu_util
    kudu_test_util)

  target_link_libraries(twitter_demo
    kudu_client
    ${LIBOAUTH_LIBRARY}
    ${CURL_LIBRARIES}
    ${KUDU_BASE_LIBS})

  # Require that the tserver protobuf code is generated first
  add_dependencies(twitter_demo
    tserver_proto)

  add_executable(ingest_firehose ingest_firehose.cc)
  target_link_libraries(ingest_firehose
    twitter_demo)

  # Tests
  ADD_KUDU_TEST(oauth-test)
  # parser-test relies on symlinked data files which we can't currently copy correctly
  # to the cluster.
  ADD_KUDU_TEST(parser-test LABELS no_dist_test)
  if(NOT "${NO_TESTS}")
    target_link_libraries(oauth-test
      twitter_demo)
    target_link_libraries(parser-test
      twitter_demo)
    execute_process(COMMAND ln -sf ${CMAKE_CURRENT_SOURCE_DIR}/example-tweets.txt
      ${EXECUTABLE_OUTPUT_PATH})
    execute_process(COMMAND ln -sf ${CMAKE_CURRENT_SOURCE_DIR}/example-deletes.txt
      ${EXECUTABLE_OUTPUT_PATH})
  endif()

endif() # library checks
