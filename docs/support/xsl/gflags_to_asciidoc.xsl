<?xml version="1.0"?>
<!--

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
-->

<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" version="1.0">
<xsl:param name="binary"/>
<xsl:param name="support-level"/> <!-- either 'stable' or 'unsupported' -->
<xsl:output method="text"/>

<!-- Normalize space -->
<xsl:template match="text()">
    <xsl:if test="normalize-space(.)">
      <xsl:value-of select="normalize-space(.)"/>
    </xsl:if>
</xsl:template>

<!-- Grab nodes of the <configuration> element -->
<xsl:template match="AllFlags">
<!-- Inject the license text into the header of each file -->
////
//
// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
////

:author: Kudu Team
:imagesdir: ./images
:icons: font
:toc: left
:toclevels: 2
:doctype: book
:backend: html5
:sectlinks:
:experimental:

<xsl:if test="$support-level = 'stable'">
[[<xsl:value-of select="$binary"/>_stable]]
= `<xsl:value-of select="$binary"/>` Flags

== Stable Flags

Flags tagged `stable` and not `advanced` are safe to use for common
configuration tasks.

<xsl:for-each select="flag"><xsl:if test="contains(tags, 'stable') and not(contains(tags, 'advanced')) and not(contains(tags, 'hidden'))">
[[<xsl:value-of select="$binary"/>_<xsl:value-of select="name"/>]]
=== `--<xsl:value-of select="name"/>`

<xsl:value-of select="meaning"/>

[cols="1h,3d", width="50%"]
|===
| Type | <xsl:value-of select="type"/>
| Default | <xsl:choose><xsl:when test="default != ''">`<xsl:value-of select="default"/>`</xsl:when><xsl:otherwise>none</xsl:otherwise></xsl:choose>
| Tags | <xsl:value-of select="tags"/>
|===
{nbsp}

</xsl:if>
</xsl:for-each>


[[<xsl:value-of select="$binary"/>_stable_advanced]]
== Stable, Advanced Flags

Flags tagged `stable` and `advanced` are supported, but should be considered
"expert" options and should be used carefully and after thorough testing.

<xsl:for-each select="flag"><xsl:if test="contains(tags, 'stable') and contains(tags, 'advanced') and not(contains(tags, 'hidden'))">
[[<xsl:value-of select="$binary"/>_<xsl:value-of select="name"/>]]
=== `--<xsl:value-of select="name"/>`

<xsl:value-of select="meaning"/>

[cols="1h,3d", width="50%"]
|===
| Type | <xsl:value-of select="type"/>
| Default | <xsl:choose><xsl:when test="default != ''">`<xsl:value-of select="default"/>`</xsl:when><xsl:otherwise>none</xsl:otherwise></xsl:choose>
| Tags | <xsl:value-of select="tags"/>
|===
{nbsp}

</xsl:if>
</xsl:for-each>
'''
</xsl:if>
<!--end stable -->

<!-- start unsupported -->
<xsl:if test="$support-level = 'unsupported'">
[[<xsl:value-of select="$binary"/>_unsupported]]
= `<xsl:value-of select="$binary"/>` Unsupported Flags

Flags marked `advanced` or `experimental` and not marked `stable`, or flags with no stability tag, are *unsupported* and are included
for informational purposes only. They are subject to change or be removed without notice.

<xsl:for-each select="flag"><xsl:if test="not(contains(tags, 'stable')) and (contains(tags, 'advanced') or contains(tags, 'experimental')) and not(contains(tags, 'hidden'))">
[[<xsl:value-of select="$binary"/>_<xsl:value-of select="name"/>]]
== `--<xsl:value-of select="name"/>`

<xsl:value-of select="meaning"/>

[cols="1h,3d", width="50%"]
|===
| Type | <xsl:value-of select="type"/>
| Default | <xsl:choose><xsl:when test="default != ''">`<xsl:value-of select="default"/>`</xsl:when><xsl:otherwise>none</xsl:otherwise></xsl:choose>
| Tags | <xsl:value-of select="tags"/>
|===
</xsl:if>
</xsl:for-each>
'''
</xsl:if>
<!-- end unsupported -->
</xsl:template>
</xsl:stylesheet>
