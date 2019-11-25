# Yugabyte-DB Distribution Packaging.
This document has details of Yugabyte-DB packaging details. We are going to distribute yugabyte in the form of Linux supported package `brew`, `deb` and `rpm` files. This will help a new user to install and configure yugabyte on Linux system in an easy way. Using a few Linux command user will able to install yugabyte-db on his local system or Linux servers. 

# `deb` package Distribustion.
 To distribute `deb` package, there is a number of the way by which we can achieve this. List of different ways are listed here
 - Adding Yugabyte `deb` package to Debian.
 - Creating a Yugabyte Package Repository.

# Adding Yugabyte Package to Debian. 
To add Yugabyte Debian package into Debian repository there are two ways. 
 - Packages Yourself
 - Getting a Sponsor and Uploading
 - Personal Package Archive (PPA)

#### Distributing Packages Yourself
 For this, we have to create a Debian developer account and then we have to fill an ITP (Intent to Package). It is a special bug report stating that we want to package Yugabyte so that other developer can know that we are working on it. 

 `Note:- As a Debian developer we need to have some reputation in Debian community then only we can become Debian Maintainer`

#### Getting a Sponsor and Uploading
 As long as we aren't an accepted Debian Developer, we cannot upload to the Debian archive directly. So we have to package our product and to ask for a sponsor on the debian-mentors mailing list. A sponsor is a (generally) seasoned Debian developer who will check our packages and help us until he thinks they are ready to be accepted into Debian. Then he will upload them and they will be added to Debian. 

#### Personal Package Archive (PPA)
 Using Personal Package Archiver (PPA) we can distribute Yugabyte and its update to Ubuntu user directly. It means users can install Yugabyte in just the same way as they install another ubuntu package. To Create PPA look [here](https://help.launchpad.net/Packaging/PPA) 

 # Refrances 
 - https://www.debian.org/doc/manuals/distribute-deb/distribute-deb.html#distributing-packages-yourself
 - https://wiki.debian.org/DebianRepository/Setup?action=show&redirect=HowToSetupADebianRepository
 - https://www.debian.org/doc/devel-manuals#maint-guide
 - https://askubuntu.com/questions/71510/how-do-i-create-a-ppa
 - https://help.launchpad.net/Packaging/PPA

