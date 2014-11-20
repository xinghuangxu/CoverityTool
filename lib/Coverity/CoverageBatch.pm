#!/usr/bin/perl
# NAME: CoverageBatch.pm
# SUMMARY: Create Coverage Batch Import file for Coverity
# UPDATE DATE: Feb 19, 2014
# PROGRAMMER: Leon Xu (leonx@netapp.com)
#
# Copyright NetApp, Inc. 2014. All rights reserved.
#
# Input Parameters: See sub new() below
#


package Coverity::CoverageBatch;

use strict;
use warnings;
use File::Basename;

require Exporter;

our @ISA = qw(Exporter);

# Items to export into callers namespace by default. Note: do not export
# names by default without a very good reason. Use EXPORT_OK instead.
# Do not simply export all your public functions/methods/constants.

# This allows declaration	use Coverity::CoverageBatch ':all';
# If you do not need this, moving things directly into @EXPORT or @EXPORT_OK
# will save memory.
our %EXPORT_TAGS = ( 'all' => [ qw(
	
) ] );

our @EXPORT_OK = ( @{ $EXPORT_TAGS{'all'} } );

our @EXPORT = qw(
	
);

our $VERSION = '0.01';

#start getter/setter
sub gcdaFolder {
    my $self = shift;
    if(@_) {
            $self->{gcdaFolder} = $_[0];
    }
    return $self->{gcdaFolder};
}
sub gcnoFolder {
    my $self = shift;
    if(@_) {
            $self->{gcnoFolder} = $_[0];
    }
    return $self->{gcnoFolder};
}
sub testname {
    my $self = shift;
    if(@_) {
            $self->{testname} = $_[0];
    }
    return $self->{testname};
}
sub suitename {
    my $self = shift;
    if(@_) {
            $self->{suitename} = $_[0];
    }
    return $self->{suitename};
}
sub teststart {
    my $self = shift;
    if(@_) {
            $self->{teststart} = $_[0];
    }
    return $self->{teststart};
}
sub output {
    my $self = shift;
    if(@_) {
            $self->{output} = $_[0];
    }
    return $self->{output};
}
#end getter/setter

#constructor
sub new($$){
	my ($class,$param)=@_;
	return bless $param, $class;
}

#main, create a batch file for a test
sub create{
	my $self=shift;
	my $filename = $self->output;
	open(my $fh, '>'.$filename) or die "Could not open file '$filename' $!";
	my $suitnameLength=length($self->suitename), my $testnameLength=length($self->testname), my $teststartLength=length($self->teststart);
	my $suitename=$self->suitename;
	my $testname=$self->testname;
	my $teststart=$self->teststart;
	print $fh <<HEADER;
suitename:$suitnameLength:$suitename
testname:$testnameLength:$testname
teststart:$teststartLength:$teststart
verbose
HEADER
	$self->traverse_gcda($self->gcdaFolder,$fh);
	close $fh;
}

#traverse and find all gcda files in a folder
sub traverse_gcda($$$){
	my($self,$folder,$fileHandler)=@_;
	if($self->is_gcda_file($folder)){
		$self->prepare_batch($folder,$fileHandler);
	}
	return if not -d $folder;
	opendir my $dh, $folder or die;
	while(my $sub= readdir $dh){
		next if $sub eq '.' or $sub eq '..';
		$self->traverse_gcda("$folder/$sub",$fileHandler);
	}
	close $dh;
	return;
}

#check if a file name ends in gcda
sub is_gcda_file($$){
	my($self,$filename)=@_;
	my $char='.';
	my $lindex=rindex($filename,$char);
	my $ext=substr $filename, $lindex+1;
	return 1 if $ext eq 'gcda';
	return 0;
}

#get relative path Example: 'aa/application/aa/bb.gcda' relative to 'application' folder will give you '/aa/bb.gcda'
sub get_relative_path($$){
	my($filename,$relFolderName)=@_;
	my $index=rindex($filename,$relFolderName);
	return substr $filename, $index+length($relFolderName);
}

#get gcno path based on gcda file path
sub get_gcno_path($$){
	my ($self,$gcdaFile) = @_;
	my $relativePath = get_relative_path($gcdaFile,'Application');
	$relativePath =~ s/\.gcda/\.gcno/;
	return $self->gcnoFolder.$relativePath;
}

#generate process command for one gcda file and write to output file
sub prepare_batch($$$){
	my($self,$gcdaFile,$fileHandler)=@_;
	my $filenameLength=length($gcdaFile);
	my $gcdaStr="gcda:".$filenameLength.":".$gcdaFile."\n";
	my $gcnoFile=$self->get_gcno_path($gcdaFile);
	my $gcnoStr="gcno:".length($gcnoFile).":".$gcnoFile."\n";
	my $compilationStr="compilation-directory:".length(dirname( $gcnoFile )).":".dirname( $gcnoFile )."\n";
	print $fileHandler $gcdaStr.$gcnoStr.$compilationStr."run\n";
}

1;
