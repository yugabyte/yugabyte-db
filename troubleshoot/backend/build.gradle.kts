import org.gradle.api.tasks.testing.logging.TestExceptionFormat
import org.gradle.api.tasks.testing.logging.TestLogEvent
import java.io.PrintStream
import com.github.jk1.license.LicenseReportExtension
import com.github.jk1.license.render.ReportRenderer
import com.github.jk1.license.render.InventoryHtmlReportRenderer
import com.github.jk1.license.filter.DependencyFilter
import com.github.jk1.license.filter.LicenseBundleNormalizer
import com.github.jk1.license.filter.ExcludeTransitiveDependenciesFilter
import com.github.jk1.license.filter.ReduceDuplicateLicensesFilter
import org.gradle.kotlin.dsl.licenseReport

plugins {
  id("org.springframework.boot") version "3.2.0"
  id("io.spring.dependency-management") version "1.1.4"
  id("com.diffplug.spotless") version "6.19.0"
  id("org.openapi.generator") version "6.5.0"
  id("com.github.jk1.dependency-license-report") version "2.5"
  id("java")
  application
}

group = "com.yugabyte.troubleshoot"
version = "0.0.1-SNAPSHOT"

java {
    toolchain {
        languageVersion = JavaLanguageVersion.of(17)
    }
}

repositories {
  mavenCentral()
  maven(url = "https://nexus.dev.yugabyte.com/repository/maven-central")
  mavenLocal()
}

spotless {
  java {
    removeUnusedImports()
    googleJavaFormat()
    target("src/**/*.java")
    importOrder("\\#","")
  }
}

application {
  mainClass.set("com.yugabyte.troubleshoot.ts.TsApplication")
}

val dependenciesList = dependencies {
  implementation("org.springframework.boot:spring-boot-starter-web")
  testImplementation("org.springframework.boot:spring-boot-starter-test")
}

openApiGenerate {
  generatorName.set("spring")
  inputSpec.set("$rootDir/src/main/resources/openapi.yaml")
  outputDir.set("$rootDir")
  apiPackage.set("com.yugabyte.troubleshoot.ts.api")
  modelPackage.set("com.yugabyte.troubleshoot.ts.model")
  invokerPackage.set("com.yugabyte.troubleshoot.ts")
  typeMappings.set(
    mapOf(
      "LocalData" to "java.util.Date"
    )
  )
  configOptions.set(
    mapOf(
      "interfaceOnly" to "true",
      "async" to "true"
    )
  )
  additionalProperties.set(
    mapOf(
      "hideGenerationTimestamp" to "true",
      "templateDir" to "./templates"
    )
  )
}

fun getStringWithColor(content: String, resultType: TestResult.ResultType, bold: Boolean): String {
  val reset = "\u001B[0m"
  var color = reset
  if (resultType == TestResult.ResultType.SUCCESS) {
    color = "\u001B[${if (bold) "1;" else "0;"}32m"
  } else if (resultType == TestResult.ResultType.SKIPPED) {
    color = "\u001B[${if (bold) "1;" else "0;"}33m"
  } else if (resultType == TestResult.ResultType.FAILURE) {
    color = "\u001B[${if (bold) "1;" else "0;"}31m"
  }

  return "$color$content$reset"
}

fun getResultWithColor(resultType: TestResult.ResultType): String {
  return getStringWithColor(resultType.toString(), resultType, true /* bold */)
}

fun prettyPrintHeader(header: String, bold: Boolean, repeatAdjustment: Int, printStream: PrintStream) {
  val startItem = "| "
  val endItem = " |"
  val repeatLength = startItem.length + header.length + endItem.length + repeatAdjustment
  var output = "\n" + "-".repeat(repeatLength) + "\n" + startItem + header + endItem + "\n" + "-".repeat(repeatLength)
  if (printStream == System.err) {
    output = getStringWithColor(output, TestResult.ResultType.FAILURE, bold)
  }

  printStream.println(output)
}

// Keep track of failed tests to output at the end.
val failedTestsToLog: MutableSet<String> = mutableSetOf()

tasks.withType<Test> {
  useJUnitPlatform()

    // We don't want tests to be cached + not run.
  outputs.upToDateWhen { false }

  // Configure test logging to be more similar to what we are used to with SBT tests.
  testLogging {
    events = setOf(
        TestLogEvent.STARTED,
        TestLogEvent.FAILED,
        TestLogEvent.PASSED,
        TestLogEvent.SKIPPED
    )

    exceptionFormat = TestExceptionFormat.FULL
    showExceptions = true
    showCauses = true
    showStackTraces = true
    // Use this when debugging tests to add print statements
    // showStandardStreams = true

    debug {
      events = setOf(
          TestLogEvent.STARTED,
          TestLogEvent.FAILED,
          TestLogEvent.PASSED,
          TestLogEvent.SKIPPED,
          TestLogEvent.STANDARD_ERROR,
          TestLogEvent.STANDARD_OUT
      )
      exceptionFormat = TestExceptionFormat.FULL
    }

    info {
      events = debug.events
      info.exceptionFormat = debug.exceptionFormat
    }

    // If a test fails, make sure to keep track of it to output at the end of the run.
    afterTest(KotlinClosure2({desc: TestDescriptor, result: TestResult ->
      if (result.resultType == TestResult.ResultType.FAILURE) {
        failedTestsToLog.add(desc.getClassName() + "." + desc.getName())
      }
    }))

    // At the end of the test run, output final results.
    afterSuite(KotlinClosure2({desc: TestDescriptor, result: TestResult ->
      if (desc.parent == null) {
        // Print out which tests failed.
        if (result.failedTestCount > 0) {
          prettyPrintHeader("Failed Tests" /* header */, true /* bold */, 0 /* repeatAdjustment */, System.err /* printStream */)
          for (failure in failedTestsToLog) {
            System.err.println(getStringWithColor(failure, TestResult.ResultType.FAILURE, false /* bold */))
          }
        }

        // Print out a full summary of the test suite.
        prettyPrintHeader(
            "Results: ${getResultWithColor(result.resultType)} (total: ${result.testCount}, passed: ${result.successfulTestCount}, failed: ${result.failedTestCount}, skipped: ${result.skippedTestCount})" /* header */,
            false /* bold */,
            -11 /* repeatAdjustment */,
            System.out /* printStream */
        )
      }
    }))
  }
}

tasks.register("cleanAllLicenseReport", Delete::class) {
  delete("$rootDir/license/all-dependencies")
}

tasks.register("cleanNonTransientLicenseReport", Delete::class) {
  delete("$rootDir/license/non-transient")
}

val dependenciesForLicenseReport = configurations.create("dependenciesForLicenseReport") {
  extendsFrom(configurations.getByName("implementation"))
}

val generateLicenseReportAllDependencies = tasks.register("generateLicenseReportTransitive") {
  group = "License Reports"
  description = "Generate All Dependencies License Report"
  dependsOn("cleanAllLicenseReport")
/*  outputs.upToDateWhen {
    file("$rootDir/license/all-dependencies/transitive-licenses.html").exists()
  }*/
  doLast {
    licenseReport {
      outputDir = "$rootDir/license/all-dependencies"
      configurations {
        create("customAllDependencyConfiguration") {
          extendsFrom(dependenciesForLicenseReport.defaultDependencies {dependenciesList})
        }
      }
      //configurations = arrayOf("compileClasspath", "runtimeClasspath")
      renderers = arrayOf<ReportRenderer>(
        InventoryHtmlReportRenderer("all-licenses.html", "Discovery Labs All Dependencies Licenses Report"))
      filters = arrayOf<DependencyFilter>(LicenseBundleNormalizer())
      excludeOwnGroup = true
      projects = arrayOf(project)
    }
  }
}

val generateLicenseReportNonTransientDependencies = tasks.register("generateLicenseReportNonTransitive") {
  group = "License Reports"
  description = "Generate Non-Transient Dependencies License Report"
  dependsOn("cleanNonTransientLicenseReport") // Make sure to depend on the cleaning task

  doLast {
    licenseReport {
      outputDir = file("$rootDir/license/non-transient").toString()
      renderers = arrayOf(
        InventoryHtmlReportRenderer("non-transient-licenses.html", "Discovery Labs Non-Transient Dependencies Licenses Report")
      )
      filters = arrayOf(
        LicenseBundleNormalizer(),
        ReduceDuplicateLicensesFilter(),
        ExcludeTransitiveDependenciesFilter()
      )
      configurations {
        create("customNonTransientDependencyConfiguration") {
          extendsFrom(dependenciesForLicenseReport.defaultDependencies { dependenciesList })
        }
      }
      excludeOwnGroup = true
      projects = arrayOf(project)
    }
  }
}

tasks.named("generateLicenseReport") {
  dependsOn("generateLicenseReportTransitive", "generateLicenseReportNonTransitive")
}
