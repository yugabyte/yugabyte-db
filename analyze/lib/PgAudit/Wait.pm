####################################################################################################################################
# WAIT MODULE
####################################################################################################################################
package PgAudit::Wait;

use strict;
use warnings FATAL => qw(all);
use Carp qw(confess);

use Exporter qw(import);
    our @EXPORT = qw();
use File::Basename qw(dirname);
use POSIX qw(ceil);
use Time::HiRes qw(gettimeofday usleep);

####################################################################################################################################
# Boolean constants
####################################################################################################################################
use constant
{
    true    => 1,
    false   => 0
};

####################################################################################################################################
# waitRemainder
####################################################################################################################################
sub waitRemainder
{
    my $lTimeBegin = gettimeofday();
    my $lSleepMs = ceil(((int($lTimeBegin) + 1.05) - $lTimeBegin) * 1000);

    usleep($lSleepMs * 1000);

    return int($lTimeBegin);
}

push @EXPORT, qw(waitRemainder);

####################################################################################################################################
# waitHiRes
####################################################################################################################################
sub waitHiRes
{
    my $fSecond = shift;

    return usleep($fSecond * 1000000);
}

push @EXPORT, qw(waitHiRes);

####################################################################################################################################
# waitInit
####################################################################################################################################
sub waitInit
{
    my $fWaitTime = shift;
    my $fSleep = shift;

    # Declare oWait hash
    my $oWait = {};

    # If wait seconds is not defined or 0 then return undef
    if (!defined($fWaitTime) || $fWaitTime == 0)
    {
        return undef;
    }

    # Wait seconds can be a minimum of .1
    if ($fWaitTime < .1)
    {
        confess 'fWaitTime cannot be < .1';
    }

    # If fSleep is not defined set it
    if (!defined($fSleep))
    {
        if ($fWaitTime >= 1)
        {
            $$oWait{sleep} = .1;
        }
        else
        {
            $$oWait{sleep} = $fWaitTime / 10;
        }
    }
    # Else make sure it's not greater than fWaitTime
    else
    {
        # Make sure fsleep is less than fWaitTime
        if ($fSleep >= $fWaitTime)
        {
            confess 'fSleep > fWaitTime - this is useless';
        }
    }

    # Set variables
    $$oWait{wait_time} = $fWaitTime;
    $$oWait{time_begin} = gettimeofday();
    $$oWait{time_end} = $$oWait{time_begin};

    return $oWait;
}

push @EXPORT, qw(waitInit);

####################################################################################################################################
# waitMore
####################################################################################################################################
sub waitMore
{
    my $oWait = shift;

    # Return if oWait is not defined
    if (!defined($oWait))
    {
        return false;
    }

    # Sleep for fSleep time
    waitHiRes($$oWait{sleep});

    # Capture the end time
    $$oWait{time_end} = gettimeofday();

    # Exit if wait time has expired
    if ((gettimeofday() - $$oWait{time_begin}) < $$oWait{wait_time})
    {
        return true;
    }

    # Else calculate the new sleep time
    my $fSleepNext = $$oWait{sleep} + (defined($$oWait{sleep_prev}) ? $$oWait{sleep_prev} : 0);

    if ($fSleepNext > $$oWait{wait_time} - ($$oWait{time_end} - $$oWait{time_begin}))
    {
        $fSleepNext = ($$oWait{wait_time} - ($$oWait{time_end} - $$oWait{time_begin})) + .001
    }

    $$oWait{sleep_prev} = $$oWait{sleep};
    $$oWait{sleep} = $fSleepNext;

    return false;
}

push @EXPORT, qw(waitMore);

####################################################################################################################################
# waitInterval
####################################################################################################################################
sub waitInterval
{
    my $oWait = shift;

    # Error if oWait is not defined
    if (!defined($oWait))
    {
        confess 'oWait is not defined';
    }

    return int(($$oWait{time_end} - $$oWait{time_begin}) * 1000) / 1000;
}

push @EXPORT, qw(waitInterval);

1;
