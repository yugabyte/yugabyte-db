package PgAudit::CSV;

################################################################################
#
# Text::CSV_PP - Text::CSV_XS compatible pure-Perl module
#
################################################################################
require 5.005;

use strict;
use vars qw($VERSION);
use Carp ();

$VERSION = '1.33';

sub PV  { 0 }
sub IV  { 1 }
sub NV  { 2 }

sub IS_QUOTED () { 0x0001; }
sub IS_BINARY () { 0x0002; }
sub IS_MISSING () { 0x0010; }


my $ERRORS = {
        # PP and XS
        1000 => "INI - constructor failed",
        1001 => "sep_char is equal to quote_char or escape_char",
        1002 => "INI - allow_whitespace with escape_char or quote_char SP or TAB",
        1003 => "INI - \r or \n in main attr not allowed",

        2010 => "ECR - QUO char inside quotes followed by CR not part of EOL",
        2011 => "ECR - Characters after end of quoted field",

        2021 => "EIQ - NL char inside quotes, binary off",
        2022 => "EIQ - CR char inside quotes, binary off",
        2025 => "EIQ - Loose unescaped escape",
        2026 => "EIQ - Binary character inside quoted field, binary off",
        2027 => "EIQ - Quoted field not terminated",

        2030 => "EIF - NL char inside unquoted verbatim, binary off",
        2031 => "EIF - CR char is first char of field, not part of EOL",
        2032 => "EIF - CR char inside unquoted, not part of EOL",
        2034 => "EIF - Loose unescaped quote",
        2037 => "EIF - Binary character in unquoted field, binary off",

        2110 => "ECB - Binary character in Combine, binary off",

        2200 => "EIO - print to IO failed. See errno",

        # PP Only Error
        4002 => "EIQ - Unescaped ESC in quoted field",
        4003 => "EIF - ESC CR",
        4004 => "EUF - ",

        # Hash-Ref errors
        3001 => "EHR - Unsupported syntax for column_names ()",
        3002 => "EHR - getline_hr () called before column_names ()",
        3003 => "EHR - bind_columns () and column_names () fields count mismatch",
        3004 => "EHR - bind_columns () only accepts refs to scalars",
        3006 => "EHR - bind_columns () did not pass enough refs for parsed fields",
        3007 => "EHR - bind_columns needs refs to writable scalars",
        3008 => "EHR - unexpected error in bound fields",
        3009 => "EHR - print_hr () called before column_names ()",
        3010 => "EHR - print_hr () called with invalid arguments",

        0    => "",
};


my $last_new_error = '';
my $last_new_err_num;

my %def_attr = (
    quote_char          => '"',
    escape_char         => '"',
    sep_char            => ',',
    eol                 => defined $\ ? $\ : '',
    always_quote        => 0,
    binary              => 0,
    keep_meta_info      => 0,
    allow_loose_quotes  => 0,
    allow_loose_escapes => 0,
    allow_unquoted_escape => 0,
    allow_whitespace    => 0,
    chomp_verbatim      => 0,
    types               => undef,
    verbatim            => 0,
    blank_is_undef      => 0,
    empty_is_undef      => 0,
    auto_diag           => 0,
    quote_space         => 1,
    quote_null          => 1,
    quote_binary        => 1,
    diag_verbose        => 0,
    decode_utf8         => 1,

    _EOF                => 0,
    _RECNO              => 0,
    _STATUS             => undef,
    _FIELDS             => undef,
    _FFLAGS             => undef,
    _STRING             => undef,
    _ERROR_INPUT        => undef,
    _ERROR_DIAG         => undef,

    _COLUMN_NAMES       => undef,
    _BOUND_COLUMNS      => undef,
);


BEGIN {
    if ( $] < 5.006 ) {
        $INC{'bytes.pm'} = 1 unless $INC{'bytes.pm'}; # dummy
        no strict 'refs';
        *{"utf8::is_utf8"} = sub { 0; };
        *{"utf8::decode"}  = sub { };
    }
    elsif ( $] < 5.008 ) {
        no strict 'refs';
        *{"utf8::is_utf8"} = sub { 0; };
        *{"utf8::decode"}  = sub { };
    }
    elsif ( !defined &utf8::is_utf8 ) {
       require Encode;
       *utf8::is_utf8 = *Encode::is_utf8;
    }

    eval q| require Scalar::Util |;
    if ( $@ ) {
        eval q| require B |;
        if ( $@ ) {
            Carp::croak $@;
        }
        else {
            *Scalar::Util::readonly = sub (\$) {
                my $b = B::svref_2object( $_[0] );
                $b->FLAGS & 0x00800000; # SVf_READONLY?
            }
        }
    }
}

################################################################################
# version
################################################################################
sub version {
    return $VERSION;
}
################################################################################
# new
################################################################################

sub _check_sanity {
    my ( $self ) = @_;

    for ( qw( sep_char quote_char escape_char ) ) {
        ( exists $self->{$_} && defined $self->{$_} && $self->{$_} =~ m/[\r\n]/ ) and return 1003;
    }

    if ( $self->{allow_whitespace} and
           ( defined $self->{quote_char}  && $self->{quote_char}  =~ m/^[ \t]$/ )
           ||
           ( defined $self->{escape_char} && $self->{escape_char} =~ m/^[ \t]$/ )
    ) {
       #$last_new_error = "INI - allow_whitespace with escape_char or quote_char SP or TAB";
       #$last_new_err_num = 1002;
       return 1002;
    }

    return 0;
}


sub new {
    my $proto = shift;
    my $attr  = @_ > 0 ? shift : {};

    $last_new_error   = 'usage: my $csv = Text::CSV_PP->new ([{ option => value, ... }]);';
    $last_new_err_num = 1000;

    return unless ( defined $attr and ref($attr) eq 'HASH' );

    my $class = ref($proto) || $proto or return;
    my $self  = { %def_attr };

    for my $prop (keys %$attr) { # if invalid attr, return undef
        unless ($prop =~ /^[a-z]/ && exists $def_attr{$prop}) {
            $last_new_error = "INI - Unknown attribute '$prop'";
            error_diag() if $attr->{ auto_diag };
            return;
        }
        $self->{$prop} = $attr->{$prop};
    }

    my $ec = _check_sanity( $self );

    if ( $ec ) {
        $last_new_error   = $ERRORS->{ $ec };
        $last_new_err_num = $ec;
        return;
        #$class->SetDiag ($ec);
    }

    $last_new_error = '';

    defined $\ and $self->{eol} = $\;

    bless $self, $class;

    $self->types( $self->{types} ) if( exists( $self->{types} ) );

    return $self;
}
################################################################################
# status
################################################################################
sub status {
    $_[0]->{_STATUS};
}
################################################################################
# error_input
################################################################################
sub error_input {
    $_[0]->{_ERROR_INPUT};
}
################################################################################
# error_diag
################################################################################
sub error_diag {
    my $self = shift;
    my @diag = (0, $last_new_error, 0);

    unless ($self and ref $self) {	# Class method or direct call
        $last_new_error and $diag[0] = defined $last_new_err_num ? $last_new_err_num : 1000;
    }
    elsif ( $self->isa (__PACKAGE__) and defined $self->{_ERROR_DIAG} ) {
        @diag = ( 0 + $self->{_ERROR_DIAG}, $ERRORS->{ $self->{_ERROR_DIAG} } );
        exists $self->{_ERROR_POS} and $diag[2] = 1 + $self->{_ERROR_POS};
    }

    my $context = wantarray;

    my $diagobj = bless \@diag, 'Text::CSV::ErrorDiag';

    unless (defined $context) { # Void context
        if ( $diag[0] ) {
            my $msg = "# CSV_PP ERROR: " . $diag[0] . " - $diag[1]\n";
            ref $self ? ( $self->{auto_diag} > 1 ? die $msg : warn $msg )
                      : warn $msg;
        }
        return;
    }

    return $context ? @diag : $diagobj;
}

sub record_number {
    return shift->{_RECNO};
}

################################################################################
# string
################################################################################
*string = \&_string;
sub _string {
    defined $_[0]->{_STRING} ? ${ $_[0]->{_STRING} } : undef;
}
################################################################################
# fields
################################################################################
*fields = \&_fields;
sub _fields {
    ref($_[0]->{_FIELDS}) ?  @{$_[0]->{_FIELDS}} : undef;
}
################################################################################
# combine
################################################################################
*combine = \&_combine;
sub _combine {
    my ($self, @part) = @_;

    # at least one argument was given for "combining"...
    return $self->{_STATUS} = 0 unless(@part);

    $self->{_FIELDS}      = \@part;
    $self->{_ERROR_INPUT} = undef;
    $self->{_STRING}      = '';
    $self->{_STATUS}      = 0;

    my ($always_quote, $binary, $quot, $sep, $esc, $empty_is_undef, $quote_space, $quote_null, $quote_binary )
            = @{$self}{qw/always_quote binary quote_char sep_char escape_char empty_is_undef quote_space quote_null quote_binary/};

    if(!defined $quot){ $quot = ''; }

    return $self->_set_error_diag(1001) if ($sep eq $esc or $sep eq $quot);

    my $re_esc = $self->{_re_comb_escape}->{$quot}->{$esc} ||= qr/(\Q$quot\E|\Q$esc\E)/;
    my $re_sp  = $self->{_re_comb_sp}->{$sep}->{$quote_space} ||= ( $quote_space ? qr/[\s\Q$sep\E]/ : qr/[\Q$sep\E]/ );

    my $must_be_quoted;
    for my $column (@part) {

        unless (defined $column) {
            $column = '';
            next;
        }
        elsif ( !$binary ) {
            $binary = 1 if utf8::is_utf8 $column;
        }

        if (!$binary and $column =~ /[^\x09\x20-\x7E]/) {
            # an argument contained an invalid character...
            $self->{_ERROR_INPUT} = $column;
            $self->_set_error_diag(2110);
            return $self->{_STATUS};
        }

        $must_be_quoted = 0;

        if($quot ne '' and $column =~ s/$re_esc/$esc$1/g){
            $must_be_quoted++;
        }
        if($column =~ /$re_sp/){
            $must_be_quoted++;
        }

        if( $binary and $quote_null ){
            use bytes;
            $must_be_quoted++ if ( $column =~ s/\0/${esc}0/g || ($quote_binary && $column =~ /[\x00-\x1f\x7f-\xa0]/) );
        }

        if($always_quote or $must_be_quoted){
            $column = $quot . $column . $quot;
        }

    }

    $self->{_STRING} = \do { join($sep, @part) . ( defined $self->{eol} ? $self->{eol} : '' ) };
    $self->{_STATUS} = 1;

    return $self->{_STATUS};
}
################################################################################
# parse
################################################################################
my %allow_eol = ("\r" => 1, "\r\n" => 1, "\n" => 1, "" => 1);

*parse = \&_parse;

sub _parse {
    my ($self, $line) = @_;

    @{$self}{qw/_STRING _FIELDS _STATUS _ERROR_INPUT/} = ( \do{ defined $line ? "$line" : undef }, undef, 0, $line );

    return 0 if(!defined $line);

    my ($binary, $quot, $sep, $esc, $types, $keep_meta_info, $allow_whitespace, $eol, $blank_is_undef, $empty_is_undef, $unquot_esc, $decode_utf8)
         = @{$self}{
            qw/binary quote_char sep_char escape_char types keep_meta_info allow_whitespace eol blank_is_undef empty_is_undef allow_unquoted_escape decode_utf8/
           };

    $sep  = ',' unless (defined $sep);
    $esc  = "\0" unless (defined $esc);
    $quot = "\0" unless (defined $quot);

    my $quot_is_null = $quot eq "\0"; # in this case, any fields are not interpreted as quoted data.

    return $self->_set_error_diag(1001) if (($sep eq $esc or $sep eq $quot) and $sep ne "\0");

    my $meta_flag      = $keep_meta_info ? [] : undef;
    my $re_split       = $self->{_re_split}->{$quot}->{$esc}->{$sep} ||= _make_regexp_split_column($esc, $quot, $sep);
    my $re_quoted       = $self->{_re_quoted}->{$quot}               ||= qr/^\Q$quot\E(.*)\Q$quot\E$/s;
    my $re_in_quot_esp1 = $self->{_re_in_quot_esp1}->{$esc}          ||= qr/\Q$esc\E(.)/;
    my $re_in_quot_esp2 = $self->{_re_in_quot_esp2}->{$quot}->{$esc} ||= qr/[\Q$quot$esc$sep\E0]/;
    my $re_quot_char    = $self->{_re_quot_char}->{$quot}            ||= qr/\Q$quot\E/;
    my $re_esc          = $self->{_re_esc}->{$quot}->{$esc}          ||= qr/\Q$esc\E(\Q$quot\E|\Q$esc\E|\Q$sep\E|0)/;
    my $re_invalid_quot = $self->{_re_invalid_quot}->{$quot}->{$esc} ||= qr/^$re_quot_char|[^\Q$re_esc\E]$re_quot_char/;

    if ($allow_whitespace) {
        $re_split = $self->{_re_split_allow_sp}->{$quot}->{$esc}->{$sep}
                     ||= _make_regexp_split_column_allow_sp($esc, $quot, $sep);
    }
    if ($unquot_esc) {
        $re_split = $self->{_re_split_allow_unqout_esc}->{$quot}->{$esc}->{$sep}
                     ||= _make_regexp_split_column_allow_unqout_esc($esc, $quot, $sep);
    }

    my $palatable = 1;
    my @part      = ();

    my $i = 0;
    my $flag;

    if (defined $eol and $eol eq "\r") {
        $line =~ s/[\r ]*\r[ ]*$//;
    }

    if ($self->{verbatim}) {
        $line .= $sep;
    }
    else {
        if (defined $eol and !$allow_eol{$eol}) {
            $line .= $sep;
        }
        else {
            $line =~ s/(?:\x0D\x0A|\x0A)?$|(?:\x0D\x0A|\x0A)[ ]*$/$sep/;
        }
    }

    my $pos = 0;

    my $utf8 = 1 if $decode_utf8 and utf8::is_utf8( $line ); # if decode_utf8 is true(default) and UTF8 marked, flag on.

    for my $col ( $line =~ /$re_split/g ) {

        if ($keep_meta_info) {
            $flag = 0x0000;
            $flag |= IS_BINARY if ($col =~ /[^\x09\x20-\x7E]/);
        }

        $pos += length $col;

        if ( ( !$binary and !$utf8 ) and $col =~ /[^\x09\x20-\x7E]/) { # Binary character, binary off
            if ( not $quot_is_null and $col =~ $re_quoted ) {
                $self->_set_error_diag(
                      $col =~ /\n([^\n]*)/ ? (2021, $pos - 1 - length $1)
                    : $col =~ /\r([^\r]*)/ ? (2022, $pos - 1 - length $1)
                    : (2026, $pos -2) # Binary character inside quoted field, binary off
                );
            }
            else {
                $self->_set_error_diag(
                      $col =~ /\Q$quot\E(.*)\Q$quot\E\r$/   ? (2010, $pos - 2)
                    : $col =~ /\n/                          ? (2030, $pos - length $col)
                    : $col =~ /^\r/                         ? (2031, $pos - length $col)
                    : $col =~ /\r([^\r]*)/                  ? (2032, $pos - 1 - length $1)
                    : (2037, $pos - length $col) # Binary character in unquoted field, binary off
                );
            }
            $palatable = 0;
            last;
        }

        if ( ($utf8 and !$binary) and  $col =~ /\n|\0/ ) { # \n still needs binary (Text::CSV_XS 0.51 compat)
            $self->_set_error_diag(2021, $pos);
            $palatable = 0;
            last;
        }

        if ( not $quot_is_null and $col =~ $re_quoted ) {
            $flag |= IS_QUOTED if ($keep_meta_info);
            $col = $1;

            my $flag_in_quot_esp;
            while ( $col =~ /$re_in_quot_esp1/g ) {
                my $str = $1;
                $flag_in_quot_esp = 1;

                if ($str !~ $re_in_quot_esp2) {

                    unless ($self->{allow_loose_escapes}) {
                        $self->_set_error_diag( 2025, $pos - 2 ); # Needless ESC in quoted field
                        $palatable = 0;
                        last;
                    }

                    unless ($self->{allow_loose_quotes}) {
                        $col =~ s/\Q$esc\E(.)/$1/g;
                    }
                }

            }

            last unless ( $palatable );

            unless ( $flag_in_quot_esp ) {
                if ($col =~ /(?<!\Q$esc\E)\Q$esc\E/) {
                    $self->_set_error_diag( 4002, $pos - 1 ); # No escaped ESC in quoted field
                    $palatable = 0;
                    last;
                }
            }

            $col =~ s{$re_esc}{$1 eq '0' ? "\0" : $1}eg;

            if ( $empty_is_undef and length($col) == 0 ) {
                $col = undef;
            }

            if ($types and $types->[$i]) { # IV or NV
                _check_type(\$col, $types->[$i]);
            }

        }

        # quoted but invalid

        elsif ( not $quot_is_null and $col =~ $re_invalid_quot ) {

            unless ($self->{allow_loose_quotes} and $col =~ /$re_quot_char/) {
                $self->_set_error_diag(
                      $col =~ /^\Q$quot\E(.*)\Q$quot\E.$/s  ? (2011, $pos - 2)
                    : $col =~ /^$re_quot_char/              ? (2027, $pos - 1)
                    : (2034, $pos - length $col) # Loose unescaped quote
                );
                $palatable = 0;
                last;
            }

        }

        elsif ($types and $types->[$i]) { # IV or NV
            _check_type(\$col, $types->[$i]);
        }

        # unquoted

        else {

            if (!$self->{verbatim} and $col =~ /\r\n|\n/) {
                $col =~ s/(?:\r\n|\n).*$//sm;
            }

            if ($col =~ /\Q$esc\E\r$/) { # for t/15_flags : test 165 'ESC CR' at line 203
                $self->_set_error_diag( 4003, $pos );
                $palatable = 0;
                last;
            }

            if ($col =~ /.\Q$esc\E$/) { # for t/65_allow : test 53-54 parse('foo\') at line 62, 65
                $self->_set_error_diag( 4004, $pos );
                $palatable = 0;
                last;
            }

            if ( $col eq '' and $blank_is_undef ) {
                $col = undef;
            }

            if ( $empty_is_undef and length($col) == 0 ) {
                $col = undef;
            }

            if ( $unquot_esc ) {
                $col =~ s/\Q$esc\E(.)/$1/g;
            }

        }

        utf8::encode($col) if $utf8;
        if ( $decode_utf8 && defined $col && _is_valid_utf8($col) ) {
            utf8::decode($col);
        }

        push @part,$col;
        push @{$meta_flag}, $flag if ($keep_meta_info);
        $self->{ _RECNO }++;

        $i++;
    }

    if ($palatable and ! @part) {
        $palatable = 0;
    }

    if ($palatable) {
        $self->{_ERROR_INPUT} = undef;
        $self->{_FIELDS}      = \@part;

        if ( $self->{_BOUND_COLUMNS} ) {
            my @vals  = @part;
            my ( $max, $count ) = ( scalar @vals, 0 );

            if ( @{ $self->{_BOUND_COLUMNS} } < $max ) {
                $self->_set_error_diag(3006);
                return;
            }

            for ( my $i = 0; $i < $max; $i++ ) {
                my $bind = $self->{_BOUND_COLUMNS}->[ $i ];
                if ( Scalar::Util::readonly( $$bind ) ) {
                    $self->_set_error_diag(3008);
                    return;
                }
                $$bind = $vals[ $i ];
            }
        }
    }

    $self->{_FFLAGS} = $keep_meta_info ? $meta_flag : [];

    return $self->{_STATUS} = $palatable;
}


sub _make_regexp_split_column {
    my ($esc, $quot, $sep) = @_;

    if ( $quot eq '' ) {
        return qr/([^\Q$sep\E]*)\Q$sep\E/s;
    }

   return qr/(
        \Q$quot\E
            [^\Q$quot$esc\E]*(?:\Q$esc\E[\Q$quot$esc\E0][^\Q$quot$esc\E]*)*
        \Q$quot\E
        | # or
        \Q$quot\E
            (?:\Q$esc\E[\Q$quot$esc$sep\E0]|[^\Q$quot$esc$sep\E])*
        \Q$quot\E
        | # or
        [^\Q$sep\E]*
       )
       \Q$sep\E
    /xs;
}


sub _make_regexp_split_column_allow_unqout_esc {
    my ($esc, $quot, $sep) = @_;

   return qr/(
        \Q$quot\E
            [^\Q$quot$esc\E]*(?:\Q$esc\E[\Q$quot$esc\E0][^\Q$quot$esc\E]*)*
        \Q$quot\E
        | # or
        \Q$quot\E
            (?:\Q$esc\E[\Q$quot$esc$sep\E0]|[^\Q$quot$esc$sep\E])*
        \Q$quot\E
        | # or
            (?:\Q$esc\E[\Q$quot$esc$sep\E0]|[^\Q$quot$esc$sep\E])*
        | # or
        [^\Q$sep\E]*
       )
       \Q$sep\E
    /xs;
}


sub _make_regexp_split_column_allow_sp {
    my ($esc, $quot, $sep) = @_;

    # if separator is space or tab, don't count that separator
    # as whitespace  --- patched by Mike O'Sullivan
    my $ws = $sep eq ' '  ? '[\x09]'
           : $sep eq "\t" ? '[\x20]'
           : '[\x20\x09]'
           ;

    if ( $quot eq '' ) {
        return qr/$ws*([^\Q$sep\E]?)$ws*\Q$sep\E$ws*/s;
    }

    qr/$ws*
       (
        \Q$quot\E
            [^\Q$quot$esc\E]*(?:\Q$esc\E[\Q$quot\E][^\Q$quot$esc\E]*)*
        \Q$quot\E
        | # or
        [^\Q$sep\E]*?
       )
       $ws*\Q$sep\E$ws*
    /xs;
}
################################################################################
# print
################################################################################
sub print {
    my ($self, $io, $cols) = @_;

    require IO::Handle;

    if(ref($cols) ne 'ARRAY'){
        Carp::croak("Expected fields to be an array ref");
    }

    $self->_combine(@$cols) or return '';

    local $\ = '';

    $io->print( $self->_string ) or $self->_set_error_diag(2200);
}

sub print_hr {
    my ($self, $io, $hr) = @_;
    $self->{_COLUMN_NAMES} or $self->_set_error_diag(3009);
    ref $hr eq "HASH"      or $self->_set_error_diag(3010);
    $self->print ($io, [ map { $hr->{$_} } $self->column_names ]);
}
################################################################################
# getline
################################################################################
sub getline {
    my ($self, $io) = @_;

    require IO::Handle;

    $self->{_EOF} = $io->eof ? 1 : '';

    my $quot = $self->{quote_char};
    my $sep  = $self->{sep_char};
    my $re   =  defined $quot ? qr/(?:\Q$quot\E)/ : undef;

    my $eol  = $self->{eol};

    local $/ = $eol if ( defined $eol and $eol ne '' );

    my $line = $io->getline();

    # AUTO DETECTION EOL CR
    if ( defined $line and defined $eol and $eol eq '' and $line =~ /[^\r]\r[^\r\n]/ and eof ) {
        $self->{_AUTO_DETECT_CR} = 1;
        $self->{eol} = "\r";
        seek( $io, 0, 0 ); # restart
        return $self->getline( $io );
    }

    if ( $re and defined $line ) {
        LOOP: {
            my $is_continued   = scalar(my @list = $line =~ /$re/g) % 2; # if line is valid, quot is even

            if ( $self->{allow_loose_quotes } ) {
                $is_continued = 0;
            }
            elsif ( $line =~ /${re}0/ ) { # null suspicion case
                $is_continued = $line =~ qr/
                    ^
                    (
                        (?:
                            $re             # $quote
                            (?:
                                  $re$re    #    escaped $quote
                                | ${re}0    # or escaped zero
                                | [^$quot]  # or exceptions of $quote
                            )*
                            $re             # $quote
                            [^0$quot]       # non zero or $quote
                        )
                        |
                        (?:[^$quot]*)       # exceptions of $quote
                    )+
                    $
                /x ? 0 : 1;
            }

            if ( $is_continued and !$io->eof) {
                $line .= $io->getline();
                goto LOOP;
            }
        }
    }

    $line =~ s/\Q$eol\E$// if ( defined $line and defined $eol and $eol ne '' );

    $self->_parse($line);

    if ( eof ) {
        $self->{_AUTO_DETECT_CR} = 0;
    }

    return unless $self->{_STATUS};

    return $self->{_BOUND_COLUMNS} ? [] : [ $self->_fields() ];
}
################################################################################
# getline_all
################################################################################
sub getline_all {
    my ( $self, $io, $offset, $len ) = @_;
    my @list;
    my $tail;
    my $n = 0;

    $offset ||= 0;

    if ( $offset < 0 ) {
        $tail = -$offset;
        $offset = 0;
    }

    while ( my $row = $self->getline($io) ) {
        next if $offset && $offset-- > 0;               # skip
        last if defined $len && !$tail && $n >= $len;   # exceeds limit size
        push @list, $row;
        ++$n;
        if ( $tail && $n > $tail ) {
            shift @list;
        }
    }

    if ( $tail && defined $len && $n > $len ) {
        @list = splice( @list, 0, $len);
    }

    return \@list;
}
################################################################################
# getline_hr
################################################################################
sub getline_hr {
    my ( $self, $io) = @_;
    my %hr;

    unless ( $self->{_COLUMN_NAMES} ) {
        $self->SetDiag( 3002 );
    }

    my $fr = $self->getline( $io ) or return undef;

    if ( ref $self->{_FFLAGS} ) {
        $self->{_FFLAGS}[$_] = IS_MISSING for ($#{$fr} + 1) .. $#{$self->{_COLUMN_NAMES}};
    }

    @hr{ @{ $self->{_COLUMN_NAMES} } } = @$fr;

    \%hr;
}
################################################################################
# getline_hr_all
################################################################################
sub getline_hr_all {
    my ( $self, $io, @args ) = @_;
    my %hr;

    unless ( $self->{_COLUMN_NAMES} ) {
        $self->SetDiag( 3002 );
    }

    my @cn = @{$self->{_COLUMN_NAMES}};

    return [ map { my %h; @h{ @cn } = @$_; \%h } @{ $self->getline_all( $io, @args ) } ];
}
################################################################################
# column_names
################################################################################
sub column_names {
    my ( $self, @columns ) = @_;

    @columns or return defined $self->{_COLUMN_NAMES} ? @{$self->{_COLUMN_NAMES}} : undef;
    @columns == 1 && ! defined $columns[0] and return $self->{_COLUMN_NAMES} = undef;

    if ( @columns == 1 && ref $columns[0] eq "ARRAY" ) {
        @columns = @{ $columns[0] };
    }
    elsif ( join "", map { defined $_ ? ref $_ : "" } @columns ) {
        $self->SetDiag( 3001 );
    }

    if ( $self->{_BOUND_COLUMNS} && @columns != @{$self->{_BOUND_COLUMNS}} ) {
        $self->SetDiag( 3003 );
    }

    $self->{_COLUMN_NAMES} = [ map { defined $_ ? $_ : "\cAUNDEF\cA" } @columns ];
    @{ $self->{_COLUMN_NAMES} };
}
################################################################################
# bind_columns
################################################################################
sub bind_columns {
    my ( $self, @refs ) = @_;

    @refs or return defined $self->{_BOUND_COLUMNS} ? @{$self->{_BOUND_COLUMNS}} : undef;
    @refs == 1 && ! defined $refs[0] and return $self->{_BOUND_COLUMNS} = undef;

    if ( $self->{_COLUMN_NAMES} && @refs != @{$self->{_COLUMN_NAMES}} ) {
        $self->SetDiag( 3003 );
    }

    if ( grep { ref $_ ne "SCALAR" } @refs ) { # why don't use grep?
        $self->SetDiag( 3004 );
    }

    $self->{_is_bound} = scalar @refs; #pack("C", scalar @refs);
    $self->{_BOUND_COLUMNS} = [ @refs ];
    @refs;
}
################################################################################
# eof
################################################################################
sub eof {
    $_[0]->{_EOF};
}
################################################################################
# type
################################################################################
sub types {
    my $self = shift;

    if (@_) {
        if (my $types = shift) {
            $self->{'_types'} = join("", map{ chr($_) } @$types);
            $self->{'types'} = $types;
        }
        else {
            delete $self->{'types'};
            delete $self->{'_types'};
            undef;
        }
    }
    else {
        $self->{'types'};
    }
}
################################################################################
sub meta_info {
    $_[0]->{_FFLAGS} ? @{ $_[0]->{_FFLAGS} } : undef;
}

sub is_quoted {
    return unless (defined $_[0]->{_FFLAGS});
    return if( $_[1] =~ /\D/ or $_[1] < 0 or  $_[1] > $#{ $_[0]->{_FFLAGS} } );

    $_[0]->{_FFLAGS}->[$_[1]] & IS_QUOTED ? 1 : 0;
}

sub is_binary {
    return unless (defined $_[0]->{_FFLAGS});
    return if( $_[1] =~ /\D/ or $_[1] < 0 or  $_[1] > $#{ $_[0]->{_FFLAGS} } );
    $_[0]->{_FFLAGS}->[$_[1]] & IS_BINARY ? 1 : 0;
}

sub is_missing {
    my ($self, $idx, $val) = @_;
    ref $self->{_FFLAGS} &&
            $idx >= 0 && $idx < @{$self->{_FFLAGS}} or return;
    $self->{_FFLAGS}[$idx] & IS_MISSING ? 1 : 0;
}
################################################################################
# _check_type
#  take an arg as scalar reference.
#  if not numeric, make the value 0. otherwise INTEGERized.
################################################################################
sub _check_type {
    my ($col_ref, $type) = @_;
    unless ($$col_ref =~ /^[+-]?(?=\d|\.\d)\d*(\.\d*)?([Ee]([+-]?\d+))?$/) {
        Carp::carp sprintf("Argument \"%s\" isn't numeric in subroutine entry",$$col_ref);
        $$col_ref = 0;
    }
    elsif ($type == NV) {
        $$col_ref = sprintf("%G",$$col_ref);
    }
    else {
        $$col_ref = sprintf("%d",$$col_ref);
    }
}
################################################################################
# _set_error_diag
################################################################################
sub _set_error_diag {
    my ( $self, $error, $pos ) = @_;

    $self->{_ERROR_DIAG} = $error;

    if (defined $pos) {
        $_[0]->{_ERROR_POS} = $pos;
    }

    $self->error_diag() if ( $error and $self->{auto_diag} );

    return;
}
################################################################################

BEGIN {
    for my $method ( qw/always_quote binary keep_meta_info allow_loose_quotes allow_loose_escapes
                            verbatim blank_is_undef empty_is_undef quote_space quote_null
                            quote_binary allow_unquoted_escape/ ) {
        eval qq|
            sub $method {
                \$_[0]->{$method} = defined \$_[1] ? \$_[1] : 0 if (\@_ > 1);
                \$_[0]->{$method};
            }
        |;
    }
}



sub sep_char {
    my $self = shift;
    if ( @_ ) {
        $self->{sep_char} = $_[0];
        my $ec = _check_sanity( $self );
        $ec and Carp::croak( $self->SetDiag( $ec ) );
    }
    $self->{sep_char};
}

sub decode_utf8 {
    my $self = shift;
    if ( @_ ) {
        $self->{decode_utf8} = $_[0];
        my $ec = _check_sanity( $self );
        $ec and Carp::croak( $self->SetDiag( $ec ) );
    }
    $self->{decode_utf8};
}

sub quote_char {
    my $self = shift;
    if ( @_ ) {
        $self->{quote_char} = $_[0];
        my $ec = _check_sanity( $self );
        $ec and Carp::croak( $self->SetDiag( $ec ) );
    }
    $self->{quote_char};
}


sub escape_char {
    my $self = shift;
    if ( @_ ) {
        $self->{escape_char} = $_[0];
        my $ec = _check_sanity( $self );
        $ec and Carp::croak( $self->SetDiag( $ec ) );
    }
    $self->{escape_char};
}


sub allow_whitespace {
    my $self = shift;
    if ( @_ ) {
        my $aw = shift;
        $aw and
            (defined $self->{quote_char}  && $self->{quote_char}  =~ m/^[ \t]$/) ||
            (defined $self->{escape_char} && $self->{escape_char} =~ m/^[ \t]$/)
                and Carp::croak ($self->SetDiag (1002));
        $self->{allow_whitespace} = $aw;
    }
    $self->{allow_whitespace};
}


sub eol {
    $_[0]->{eol} = defined $_[1] ? $_[1] : '' if ( @_ > 1 );
    $_[0]->{eol};
}


sub SetDiag {
    if ( defined $_[1] and $_[1] == 0 ) {
        $_[0]->{_ERROR_DIAG} = undef;
        $last_new_error = '';
        return;
    }

    $_[0]->_set_error_diag( $_[1] );
    Carp::croak( $_[0]->error_diag . '' );
}

sub auto_diag {
    my $self = shift;
    if (@_) {
        my $v = shift;
        !defined $v || $v eq "" and $v = 0;
        $v =~ m/^[0-9]/ or $v = $v ? 1 : 0; # default for true/false
        $self->{auto_diag} = $v;
    }
    $self->{auto_diag};
}

sub diag_verbose {
    my $self = shift;
    if (@_) {
        my $v = shift;
        !defined $v || $v eq "" and $v = 0;
        $v =~ m/^[0-9]/ or $v = $v ? 1 : 0; # default for true/false
        $self->{diag_verbose} = $v;
    }
    $self->{diag_verbose};
}

sub _is_valid_utf8 {
    return ( $_[0] =~ /^(?:
         [\x00-\x7F]
        |[\xC2-\xDF][\x80-\xBF]
        |[\xE0][\xA0-\xBF][\x80-\xBF]
        |[\xE1-\xEC][\x80-\xBF][\x80-\xBF]
        |[\xED][\x80-\x9F][\x80-\xBF]
        |[\xEE-\xEF][\x80-\xBF][\x80-\xBF]
        |[\xF0][\x90-\xBF][\x80-\xBF][\x80-\xBF]
        |[\xF1-\xF3][\x80-\xBF][\x80-\xBF][\x80-\xBF]
        |[\xF4][\x80-\x8F][\x80-\xBF][\x80-\xBF]
    )+$/x )  ? 1 : 0;
}
################################################################################
package Text::CSV::ErrorDiag;

use strict;
use overload (
    '""' => \&stringify,
    '+'  => \&numeric,
    '-'  => \&numeric,
    '*'  => \&numeric,
    '/'  => \&numeric,
);


sub numeric {
    my ($left, $right) = @_;
    return ref $left ? $left->[0] : $right->[0];
}


sub stringify {
    $_[0]->[1];
}
################################################################################
1;
__END__

=head1 NAME

Text::CSV_PP - Text::CSV_XS compatible pure-Perl module


=head1 SYNOPSIS

 use Text::CSV_PP;

 $csv = Text::CSV_PP->new();     # create a new object
 # If you want to handle non-ascii char.
 $csv = Text::CSV_PP->new({binary => 1});

 $status = $csv->combine(@columns);    # combine columns into a string
 $line   = $csv->string();             # get the combined string

 $status  = $csv->parse($line);        # parse a CSV string into fields
 @columns = $csv->fields();            # get the parsed fields

 $status       = $csv->status ();      # get the most recent status
 $bad_argument = $csv->error_input (); # get the most recent bad argument
 $diag         = $csv->error_diag ();  # if an error occured, explains WHY

 $status = $csv->print ($io, $colref); # Write an array of fields
                                       # immediately to a file $io
 $colref = $csv->getline ($io);        # Read a line from file $io,
                                       # parse it and return an array
                                       # ref of fields
 $csv->column_names (@names);          # Set column names for getline_hr ()
 $ref = $csv->getline_hr ($io);        # getline (), but returns a hashref
 $eof = $csv->eof ();                  # Indicate if last parse or
                                       # getline () hit End Of File

 $csv->types(\@t_array);               # Set column types


=head1 DESCRIPTION

Text::CSV_PP has almost same functions of L<Text::CSV_XS> which
provides facilities for the composition and decomposition of
comma-separated values. As its name suggests, L<Text::CSV_XS>
is a XS module and Text::CSV_PP is a Puer Perl one.

=head1 VERSION

    1.31

This module is compatible with Text::CSV_XS B<0.99>.
(except for diag_verbose and allow_unquoted_escape)

=head2 Unicode (UTF8)

On parsing (both for C<getline ()> and C<parse ()>), if the source is
marked being UTF8, then parsing that source will mark all fields that
are marked binary will also be marked UTF8.

On combining (C<print ()> and C<combine ()>), if any of the combining
fields was marked UTF8, the resulting string will be marked UTF8.

=head1 FUNCTIONS

These methods are almost same as Text::CSV_XS.
Most of the documentation was shamelessly copied and replaced from Text::CSV_XS.

See to L<Text::CSV_XS>.

=head2 version ()

(Class method) Returns the current backend module version.
If you want the module version, you can use the C<VERSION> method,

 print Text::CSV->VERSION;      # This module version
 print Text::CSV->version;      # The version of the worker module
                                # same as Text::CSV->backend->version

=head2 new (\%attr)

(Class method) Returns a new instance of Text::CSV_XS. The objects
attributes are described by the (optional) hash ref C<\%attr>.
Currently the following attributes are available:

=over 4

=item eol

An end-of-line string to add to rows. C<undef> is replaced with an
empty string. The default is C<$\>. Common values for C<eol> are
C<"\012"> (Line Feed) or C<"\015\012"> (Carriage Return, Line Feed).
Cannot be longer than 7 (ASCII) characters.

If both C<$/> and C<eol> equal C<"\015">, parsing lines that end on
only a Carriage Return without Line Feed, will be C<parse>d correct.
Line endings, whether in C<$/> or C<eol>, other than C<undef>,
C<"\n">, C<"\r\n">, or C<"\r"> are not (yet) supported for parsing.

=item sep_char

The char used for separating fields, by default a comma. (C<,>).
Limited to a single-byte character, usually in the range from 0x20
(space) to 0x7e (tilde).

The separation character can not be equal to the quote character.
The separation character can not be equal to the escape character.

See also L<Text::CSV_XS/CAVEATS>

=item allow_whitespace

When this option is set to true, whitespace (TAB's and SPACE's)
surrounding the separation character is removed when parsing. If
either TAB or SPACE is one of the three major characters C<sep_char>,
C<quote_char>, or C<escape_char> it will not be considered whitespace.

So lines like:

  1 , "foo" , bar , 3 , zapp

are now correctly parsed, even though it violates the CSV specs.

Note that B<all> whitespace is stripped from start and end of each
field. That would make it more a I<feature> than a way to be able
to parse bad CSV lines, as

 1,   2.0,  3,   ape  , monkey

will now be parsed as

 ("1", "2.0", "3", "ape", "monkey")

even if the original line was perfectly sane CSV.

=item blank_is_undef

Under normal circumstances, CSV data makes no distinction between
quoted- and unquoted empty fields. They both end up in an empty
string field once read, so

 1,"",," ",2

is read as

 ("1", "", "", " ", "2")

When I<writing> CSV files with C<always_quote> set, the unquoted empty
field is the result of an undefined value. To make it possible to also
make this distinction when reading CSV data, the C<blank_is_undef> option
will cause unquoted empty fields to be set to undef, causing the above to
be parsed as

 ("1", "", undef, " ", "2")

=item empty_is_undef

Going one step further than C<blank_is_undef>, this attribute converts
all empty fields to undef, so

 1,"",," ",2

is read as

 (1, undef, undef, " ", 2)

Note that this only effects fields that are I<really> empty, not fields
that are empty after stripping allowed whitespace. YMMV.

=item quote_char

The char used for quoting fields containing blanks, by default the
double quote character (C<">). A value of undef suppresses
quote chars. (For simple cases only).
Limited to a single-byte character, usually in the range from 0x20
(space) to 0x7e (tilde).

The quote character can not be equal to the separation character.

=item allow_loose_quotes

By default, parsing fields that have C<quote_char> characters inside
an unquoted field, like

 1,foo "bar" baz,42

would result in a parse error. Though it is still bad practice to
allow this format, we cannot help there are some vendors that make
their applications spit out lines styled like this.

In case there is B<really> bad CSV data, like

 1,"foo "bar" baz",42

or

 1,""foo bar baz"",42

there is a way to get that parsed, and leave the quotes inside the quoted
field as-is. This can be achieved by setting C<allow_loose_quotes> B<AND>
making sure that the C<escape_char> is I<not> equal to C<quote_char>.

=item escape_char

The character used for escaping certain characters inside quoted fields.
Limited to a single-byte character, usually in the range from 0x20
(space) to 0x7e (tilde).

The C<escape_char> defaults to being the literal double-quote mark (C<">)
in other words, the same as the default C<quote_char>. This means that
doubling the quote mark in a field escapes it:

  "foo","bar","Escape ""quote mark"" with two ""quote marks""","baz"

If you change the default quote_char without changing the default
escape_char, the escape_char will still be the quote mark.  If instead
you want to escape the quote_char by doubling it, you will need to change
the escape_char to be the same as what you changed the quote_char to.

The escape character can not be equal to the separation character.

=item allow_loose_escapes

By default, parsing fields that have C<escape_char> characters that
escape characters that do not need to be escaped, like:

 my $csv = Text::CSV->new ({ escape_char => "\\" });
 $csv->parse (qq{1,"my bar\'s",baz,42});

would result in a parse error. Though it is still bad practice to
allow this format, this option enables you to treat all escape character
sequences equal.

=item binary

If this attribute is TRUE, you may use binary characters in quoted fields,
including line feeds, carriage returns and NULL bytes. (The latter must
be escaped as C<"0>.) By default this feature is off.

If a string is marked UTF8, binary will be turned on automatically when
binary characters other than CR or NL are encountered. Note that a simple
string like C<"\x{00a0}"> might still be binary, but not marked UTF8, so
setting C<{ binary =E<gt> 1 }> is still a wise option.

=item types

A set of column types; this attribute is immediately passed to the
I<types> method below. You must not set this attribute otherwise,
except for using the I<types> method. For details see the description
of the I<types> method below.

=item always_quote

By default the generated fields are quoted only, if they need to, for
example, if they contain the separator. If you set this attribute to
a TRUE value, then all defined fields will be quoted. This is typically
easier to handle in external applications.

=item quote_space

By default, a space in a field would trigger quotation. As no rule
exists this to be forced in CSV, nor any for the opposite, the default
is true for safety. You can exclude the space from this trigger by
setting this option to 0.

=item quote_null

By default, a NULL byte in a field would be escaped. This attribute
enables you to treat the NULL byte as a simple binary character in
binary mode (the C<{ binary =E<gt> 1 }> is set). The default is true.
You can prevent NULL escapes by setting this attribute to 0.

=item keep_meta_info

By default, the parsing of input lines is as simple and fast as
possible. However, some parsing information - like quotation of
the original field - is lost in that process. Set this flag to
true to be able to retrieve that information after parsing with
the methods C<meta_info ()>, C<is_quoted ()>, and C<is_binary ()>
described below.  Default is false.

=item verbatim

This is a quite controversial attribute to set, but it makes hard
things possible.

The basic thought behind this is to tell the parser that the normally
special characters newline (NL) and Carriage Return (CR) will not be
special when this flag is set, and be dealt with as being ordinary
binary characters. This will ease working with data with embedded
newlines.

When C<verbatim> is used with C<getline ()>, C<getline ()>
auto-chomp's every line.

Imagine a file format like

  M^^Hans^Janssen^Klas 2\n2A^Ja^11-06-2007#\r\n

where, the line ending is a very specific "#\r\n", and the sep_char
is a ^ (caret). None of the fields is quoted, but embedded binary
data is likely to be present. With the specific line ending, that
shouldn't be too hard to detect.

By default, Text::CSV' parse function however is instructed to only
know about "\n" and "\r" to be legal line endings, and so has to deal
with the embedded newline as a real end-of-line, so it can scan the next
line if binary is true, and the newline is inside a quoted field.
With this attribute however, we can tell parse () to parse the line
as if \n is just nothing more than a binary character.

For parse () this means that the parser has no idea about line ending
anymore, and getline () chomps line endings on reading.

=item auto_diag

Set to true will cause C<error_diag ()> to be automatically be called
in void context upon errors.

If set to a value greater than 1, it will die on errors instead of
warn.

To check future plans and a difference in XS version,
please see to L<Text::CSV_XS/auto_diag>.

=back

To sum it up,

 $csv = Text::CSV_PP->new ();

is equivalent to

 $csv = Text::CSV_PP->new ({
     quote_char          => '"',
     escape_char         => '"',
     sep_char            => ',',
     eol                 => $\,
     always_quote        => 0,
     quote_space         => 1,
     quote_null          => 1,
     binary              => 0,
     keep_meta_info      => 0,
     allow_loose_quotes  => 0,
     allow_loose_escapes => 0,
     allow_whitespace    => 0,
     blank_is_undef      => 0,
     empty_is_undef      => 0,
     verbatim            => 0,
     auto_diag           => 0,
     });


For all of the above mentioned flags, there is an accessor method
available where you can inquire for the current value, or change
the value

 my $quote = $csv->quote_char;
 $csv->binary (1);

It is unwise to change these settings halfway through writing CSV
data to a stream. If however, you want to create a new stream using
the available CSV object, there is no harm in changing them.

If the C<new ()> constructor call fails, it returns C<undef>, and makes
the fail reason available through the C<error_diag ()> method.

 $csv = Text::CSV->new ({ ecs_char => 1 }) or
     die "" . Text::CSV->error_diag ();

C<error_diag ()> will return a string like

 "INI - Unknown attribute 'ecs_char'"

=head2 print

 $status = $csv->print ($io, $colref);

Similar to C<combine () + string () + print>, but more efficient. It
expects an array ref as input (not an array!) and the resulting string is
not really created (XS version), but immediately written to the I<$io> object, typically
an IO handle or any other object that offers a I<print> method. Note, this
implies that the following is wrong in perl 5.005_xx and older:

 open FILE, ">", "whatever";
 $status = $csv->print (\*FILE, $colref);

as in perl 5.005 and older, the glob C<\*FILE> is not an object, thus it
doesn't have a print method. The solution is to use an IO::File object or
to hide the glob behind an IO::Wrap object. See L<IO::File> and L<IO::Wrap>
for details.

For performance reasons the print method doesn't create a result string.
(If its backend is PP version, result strings are created internally.)
In particular the I<$csv-E<gt>string ()>, I<$csv-E<gt>status ()>,
I<$csv->fields ()> and I<$csv-E<gt>error_input ()> methods are meaningless
after executing this method.

=head2 combine

 $status = $csv->combine (@columns);

This object function constructs a CSV string from the arguments, returning
success or failure.  Failure can result from lack of arguments or an argument
containing an invalid character.  Upon success, C<string ()> can be called to
retrieve the resultant CSV string.  Upon failure, the value returned by
C<string ()> is undefined and C<error_input ()> can be called to retrieve an
invalid argument.

=head2 string

 $line = $csv->string ();

This object function returns the input to C<parse ()> or the resultant CSV
string of C<combine ()>, whichever was called more recently.

=head2 getline

 $colref = $csv->getline ($io);

This is the counterpart to print, like parse is the counterpart to
combine: It reads a row from the IO object $io using $io->getline ()
and parses this row into an array ref. This array ref is returned
by the function or undef for failure.

When fields are bound with C<bind_columns ()>, the return value is a
reference to an empty list.

The I<$csv-E<gt>string ()>, I<$csv-E<gt>fields ()> and I<$csv-E<gt>status ()>
methods are meaningless, again.

=head2 getline_all

 $arrayref = $csv->getline_all ($io);
 $arrayref = $csv->getline_all ($io, $offset);
 $arrayref = $csv->getline_all ($io, $offset, $length);

This will return a reference to a list of C<getline ($io)> results.
In this call, C<keep_meta_info> is disabled. If C<$offset> is negative,
as with C<splice ()>, only the last C<abs ($offset)> records of C<$io>
are taken into consideration.

Given a CSV file with 10 lines:

 lines call
 ----- ---------------------------------------------------------
 0..9  $csv->getline_all ($io)         # all
 0..9  $csv->getline_all ($io,  0)     # all
 8..9  $csv->getline_all ($io,  8)     # start at 8
 -     $csv->getline_all ($io,  0,  0) # start at 0 first 0 rows
 0..4  $csv->getline_all ($io,  0,  5) # start at 0 first 5 rows
 4..5  $csv->getline_all ($io,  4,  2) # start at 4 first 2 rows
 8..9  $csv->getline_all ($io, -2)     # last 2 rows
 6..7  $csv->getline_all ($io, -4,  2) # first 2 of last  4 rows

=head2 parse

 $status = $csv->parse ($line);

This object function decomposes a CSV string into fields, returning
success or failure.  Failure can result from a lack of argument or the
given CSV string is improperly formatted.  Upon success, C<fields ()> can
be called to retrieve the decomposed fields .  Upon failure, the value
returned by C<fields ()> is undefined and C<error_input ()> can be called
to retrieve the invalid argument.

You may use the I<types ()> method for setting column types. See the
description below.

=head2 getline_hr

The C<getline_hr ()> and C<column_names ()> methods work together to allow
you to have rows returned as hashrefs. You must call C<column_names ()>
first to declare your column names.

 $csv->column_names (qw( code name price description ));
 $hr = $csv->getline_hr ($io);
 print "Price for $hr->{name} is $hr->{price} EUR\n";

C<getline_hr ()> will croak if called before C<column_names ()>.

=head2 getline_hr_all

 $arrayref = $csv->getline_hr_all ($io);

This will return a reference to a list of C<getline_hr ($io)> results.
In this call, C<keep_meta_info> is disabled.

=head2 column_names

Set the keys that will be used in the C<getline_hr ()> calls. If no keys
(column names) are passed, it'll return the current setting.

C<column_names ()> accepts a list of scalars (the column names) or a
single array_ref, so you can pass C<getline ()>

  $csv->column_names ($csv->getline ($io));

C<column_names ()> does B<no> checking on duplicates at all, which might
lead to unwanted results. Undefined entries will be replaced with the
string C<"\cAUNDEF\cA">, so

  $csv->column_names (undef, "", "name", "name");
  $hr = $csv->getline_hr ($io);

Will set C<$hr->{"\cAUNDEF\cA"}> to the 1st field, C<$hr->{""}> to the
2nd field, and C<$hr->{name}> to the 4th field, discarding the 3rd field.

C<column_names ()> croaks on invalid arguments.

=head2 print_hr

 $csv->print_hr ($io, $ref);

Provides an easy way to print a C<$ref> as fetched with L<getline_hr>
provided the column names are set with L<column_names>.

It is just a wrapper method with basic parameter checks over

 $csv->print ($io, [ map { $ref->{$_} } $csv->column_names ]);

=head2 bind_columns

Takes a list of references to scalars to store the fields fetched
C<getline ()> in. When you don't pass enough references to store the
fetched fields in, C<getline ()> will fail. If you pass more than there are
fields to return, the remaining references are left untouched.

  $csv->bind_columns (\$code, \$name, \$price, \$description);
  while ($csv->getline ($io)) {
      print "The price of a $name is \x{20ac} $price\n";
      }

=head2 eof

 $eof = $csv->eof ();

If C<parse ()> or C<getline ()> was used with an IO stream, this
method will return true (1) if the last call hit end of file, otherwise
it will return false (''). This is useful to see the difference between
a failure and end of file.

=head2 types

 $csv->types (\@tref);

This method is used to force that columns are of a given type. For
example, if you have an integer column, two double columns and a
string column, then you might do a

 $csv->types ([Text::CSV_PP::IV (),
               Text::CSV_PP::NV (),
               Text::CSV_PP::NV (),
               Text::CSV_PP::PV ()]);

Column types are used only for decoding columns, in other words
by the I<parse ()> and I<getline ()> methods.

You can unset column types by doing a

 $csv->types (undef);

or fetch the current type settings with

 $types = $csv->types ();

=over 4

=item IV

Set field type to integer.

=item NV

Set field type to numeric/float.

=item PV

Set field type to string.

=back

=head2 fields

 @columns = $csv->fields ();

This object function returns the input to C<combine ()> or the resultant
decomposed fields of C successful <parse ()>, whichever was called more
recently.

Note that the return value is undefined after using C<getline ()>, which
does not fill the data structures returned by C<parse ()>.

=head2 meta_info

 @flags = $csv->meta_info ();

This object function returns the flags of the input to C<combine ()> or
the flags of the resultant decomposed fields of C<parse ()>, whichever
was called more recently.

For each field, a meta_info field will hold flags that tell something about
the field returned by the C<fields ()> method or passed to the C<combine ()>
method. The flags are bitwise-or'd like:

=over 4

=item 0x0001

The field was quoted.

=item 0x0002

The field was binary.

=back

See the C<is_*** ()> methods below.

=head2 is_quoted

  my $quoted = $csv->is_quoted ($column_idx);

Where C<$column_idx> is the (zero-based) index of the column in the
last result of C<parse ()>.

This returns a true value if the data in the indicated column was
enclosed in C<quote_char> quotes. This might be important for data
where C<,20070108,> is to be treated as a numeric value, and where
C<,"20070108",> is explicitly marked as character string data.

=head2 is_binary

  my $binary = $csv->is_binary ($column_idx);

Where C<$column_idx> is the (zero-based) index of the column in the
last result of C<parse ()>.

This returns a true value if the data in the indicated column
contained any byte in the range [\x00-\x08,\x10-\x1F,\x7F-\xFF]

=head2 status

 $status = $csv->status ();

This object function returns success (or failure) of C<combine ()> or
C<parse ()>, whichever was called more recently.

=head2 error_input

 $bad_argument = $csv->error_input ();

This object function returns the erroneous argument (if it exists) of
C<combine ()> or C<parse ()>, whichever was called more recently.

=head2 error_diag

 Text::CSV_PP->error_diag ();
 $csv->error_diag ();
 $error_code   = 0  + $csv->error_diag ();
 $error_str    = "" . $csv->error_diag ();
 ($cde, $str, $pos) = $csv->error_diag ();

If (and only if) an error occurred, this function returns the diagnostics
of that error.

If called in void context, it will print the internal error code and the
associated error message to STDERR.

If called in list context, it will return the error code and the error
message in that order. If the last error was from parsing, the third
value returned is the best guess at the location within the line that was
being parsed. It's value is 1-based.

Note: C<$pos> does not show the error point in many cases.
It is for conscience's sake.

If called in scalar context, it will return the diagnostics in a single
scalar, a-la $!. It will contain the error code in numeric context, and
the diagnostics message in string context.

To achieve this behavior with CSV_PP, the returned diagnostics is blessed object.

=head2 SetDiag

 $csv->SetDiag (0);

Use to reset the diagnostics if you are dealing with errors.

=head1 DIAGNOSTICS

If an error occurred, $csv->error_diag () can be used to get more information
on the cause of the failure. Note that for speed reasons, the internal value
is never cleared on success, so using the value returned by error_diag () in
normal cases - when no error occurred - may cause unexpected results.

Note: CSV_PP's diagnostics is different from CSV_XS's:

Text::CSV_XS parses csv strings by dividing one character
while Text::CSV_PP by using the regular expressions.
That difference makes the different cause of the failure.

Currently these errors are available:

=over 2

=item 1001 "sep_char is equal to quote_char or escape_char"

The separation character cannot be equal to either the quotation character
or the escape character, as that will invalidate all parsing rules.

=item 1002 "INI - allow_whitespace with escape_char or quote_char SP or TAB"

Using C<allow_whitespace> when either C<escape_char> or C<quote_char> is
equal to SPACE or TAB is too ambiguous to allow.

=item 1003 "INI - \r or \n in main attr not allowed"

Using default C<eol> characters in either C<sep_char>, C<quote_char>, or
C<escape_char> is not allowed.

=item 2010 "ECR - QUO char inside quotes followed by CR not part of EOL"

=item 2011 "ECR - Characters after end of quoted field"

=item 2021 "EIQ - NL char inside quotes, binary off"

=item 2022 "EIQ - CR char inside quotes, binary off"

=item 2025 "EIQ - Loose unescaped escape"

=item 2026 "EIQ - Binary character inside quoted field, binary off"

=item 2027 "EIQ - Quoted field not terminated"

=item 2030 "EIF - NL char inside unquoted verbatim, binary off"

=item 2031 "EIF - CR char is first char of field, not part of EOL",

=item 2032 "EIF - CR char inside unquoted, not part of EOL",

=item 2034 "EIF - Loose unescaped quote",

=item 2037 "EIF - Binary character in unquoted field, binary off",

=item 2110 "ECB - Binary character in Combine, binary off"

=item 2200 "EIO - print to IO failed. See errno"

=item 4002 "EIQ - Unescaped ESC in quoted field"

=item 4003 "EIF - ESC CR"

=item 4004 "EUF - "

=item 3001 "EHR - Unsupported syntax for column_names ()"

=item 3002 "EHR - getline_hr () called before column_names ()"

=item 3003 "EHR - bind_columns () and column_names () fields count mismatch"

=item 3004 "EHR - bind_columns () only accepts refs to scalars"

=item 3006 "EHR - bind_columns () did not pass enough refs for parsed fields"

=item 3007 "EHR - bind_columns needs refs to writable scalars"

=item 3008 "EHR - unexpected error in bound fields"

=back

=head1 AUTHOR

Makamaka Hannyaharamitu, E<lt>makamaka[at]cpan.orgE<gt>

Text::CSV_XS was written by E<lt>joe[at]ispsoft.deE<gt>
and maintained by E<lt>h.m.brand[at]xs4all.nlE<gt>.

Text::CSV was written by E<lt>alan[at]mfgrtl.comE<gt>.


=head1 COPYRIGHT AND LICENSE

Copyright 2005-2015 by Makamaka Hannyaharamitu, E<lt>makamaka[at]cpan.orgE<gt>

This library is free software; you can redistribute it and/or modify
it under the same terms as Perl itself.

=head1 SEE ALSO

L<Text::CSV_XS>, L<Text::CSV>

I got many regexp bases from L<http://www.din.or.jp/~ohzaki/perl.htm>

=cut
