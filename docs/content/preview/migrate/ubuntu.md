---
title: Install
headerTitle: Install
linkTitle: Install
description: Explore the prerequisites, YugabyteDB Voyager installation, and so on.
beta: /preview/faq/general/#what-is-the-definition-of-the-beta-feature-tag
image: /images/section_icons/develop/learn.png
menu:
  preview:
    identifier: install-yb-voyager-ubuntu
    parent: voyager
    weight: 101
type: docs
---

Install the Yugabyte apt repository on your system. This repository contains the yb-voyager debian package itself and the dependencies required to run yb-voyager.
wget https://s3.us-west-2.amazonaws.com/downloads.yugabyte.com/repos/reporpms/yb-apt-repo_1.0.0_all.deb
sudo apt-get install ./yb-apt-repo_1.0.0_all.deb
Clean out the temporary cache of apt-get and update the package lists of apt-get on your system.
	sudo apt-get clean
	sudo apt-get update
Install yb-voyager and its dependencies:
	sudo apt-get install yb-voyager

