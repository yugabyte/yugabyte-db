
# Copyright (c) 2021-2022, PostgreSQL Global Development Group

#
# Script that provides 'make install' functionality for msvc builds
#
# src/tools/msvc/install.pl
#
use strict;
use warnings;

use FindBin;
use lib $FindBin::RealBin;

use Install qw(Install);

# buildenv.pl is for specifying the build environment settings
# it should contain lines like:
# $ENV{PATH} = "c:/path/to/bison/bin;$ENV{PATH}";

if (-e "src/tools/msvc/buildenv.pl")
{
	do "./src/tools/msvc/buildenv.pl";
}
elsif (-e "./buildenv.pl")
{
	do "./buildenv.pl";
}

my $target = shift || Usage();
my $insttype = shift;
Install($target, $insttype);

sub Usage
{
	print "Usage: install.pl <targetdir> [installtype]\n";
	print "installtype: client\n";
	exit(1);
}
