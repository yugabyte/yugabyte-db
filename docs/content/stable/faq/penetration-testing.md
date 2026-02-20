---
title: Penetration testing recommendations
headerTitle: Penetration testing
linkTitle: Penetration testing
description: Recommendations for conducting penetration testing on YugabyteDB and YugabyteDB Anywhere.
menu:
  stable_faq:
    identifier: penetration-testing
    parent: faq
    weight: 210
    params:
      classes: separator
type: docs
unversioned: true
rightNav:
  hideH4: true
---

## Disclaimer

This information is provided by YugabyteDB, Inc. ("Yugabyte") and is for general informational purposes only. All information in this Guide is provided in good faith, however we make no representation or warranty of any kind, express, or implied, regarding the accuracy, adequacy, validity, reliability, availability, or completeness of any information contained in this Guide.

## Assumptions

- Customers conducting penetration testing are expected to be familiar with YugabyteDB and YugabyteDB Anywhere. If clarification is needed, consult our public documentation.

- YugabyteDB and YugabyteDB Anywhere are assumed to be configured in accordance with Yugabyte security best practices.

## Scope

This guidance applies to YugabyteDB and YugabyteDB Anywhere.

For clarity, YugabyteDB, Inc. does not allow any security, vulnerability, or penetration testing of any kind in any of its hosted environments; this includes YugabyteDB Aeon, the Yugabyte database SaaS product offering.

## General guidelines

There are various categories of penetration tests, each aligned with a specific methodology, such as Application Security Testing, Network (Stress) Testing, DDoS Simulation, Malware Insertion, and others. To avoid service disruption, compliance violations, or data loss, observe the following best practices:

1. Define the target system.

    Clearly define the scope and target of the penetration test. For example, YugabyteDB Anywhere interface, database nodes, network access, and so on.

1. Prefer non-production environments.

    Whenever possible:

    - Perform tests in staging, QA, or dedicated security-testing environments.
    - Avoid running penetration tests against production systems to minimize risk.

1. If production testing is unavoidable - certain tests (for example, PCI segmentation validation) may require testing in production - be sure to:

    - Notify all stakeholders, including security, legal, operations, and support teams.
    - Define a clear testing window, preferably outside of business hours or during scheduled maintenance.
    - Use test/dummy data whenever feasible.
    - Use dedicated credentials with limited scope and visibility.
    - Have a rollback or incident response plan ready.

    Optionally, reach out to {{% support-general %}} if you require urgent assistance.

1. Avoid disruptive testing.

    - Do not perform: DDoS simulations, brute-force attacks, or large-scale stress testing.
    - YugabyteDB and YugabyteDB Anywhere are typically deployed in private environments and do not include native DDoS protection.
    - If DDoS protection is required, consider deploying a third-party WAF or DDoS protection layer.
    - Prefer read-only operations when possible to reduce risk of data modification or deletion.

1. Ensure legal and regulatory compliance.

    Contact your compliance and legal team for information regarding any requirements you may have from a legal, compliance, or regulatory perspective. Legal, compliance, or regulatory requirements may require your penetration testers to hold specific certifications and/or credentials. Additionally, be aware that data exfiltration (even as part of a test) may be considered a serious security incident or breach.

1. Monitor and audit testing activities.

    - Enable audit logging on all relevant systems before the test begins.
    - Monitor real-time activity during the test and set up alerts for abnormal behavior.
