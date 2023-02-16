/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

import org.gradle.api.tasks.testing.logging.TestExceptionFormat
import org.gradle.api.tasks.testing.logging.TestLogEvent

plugins {
    `java-library`
    antlr
}

repositories {
    mavenCentral()
}

dependencies {
    implementation("org.postgresql:postgresql:42.2.20")
    api("org.apache.commons:commons-text:1.9")
    antlr("org.antlr:antlr4:4.9.2")

    testImplementation("org.junit.jupiter:junit-jupiter-api:5.7.1")
    testRuntimeOnly("org.junit.jupiter:junit-jupiter-engine")

    testImplementation("org.testcontainers:testcontainers:1.15.3")
    testImplementation("org.postgresql:postgresql:42.2.20")

    testImplementation("org.slf4j:slf4j-api:1.7.5")
    testImplementation("org.slf4j:slf4j-simple:1.7.5")
}

tasks.generateGrammarSource {
    maxHeapSize = "64m"
    source = project.objects
        .sourceDirectorySet("antlr", "antlr")
        .srcDir("${projectDir}/../../").apply {
            include("*.g4")
        }
    arguments.addAll(arrayOf("-package", "org.apache.age.jdbc.antlr4"))
    outputDirectory = file("$outputDirectory/org/apache/age/jdbc/antlr4")
}

tasks.test {
    useJUnitPlatform();
    testLogging {
        // set options for log level LIFECYCLE
        events(TestLogEvent.FAILED,
            TestLogEvent.PASSED,
            TestLogEvent.SKIPPED,
            TestLogEvent.STANDARD_OUT)
        exceptionFormat = TestExceptionFormat.FULL
        showExceptions  = true
        showCauses = true
        showStackTraces  = true
    }
}
