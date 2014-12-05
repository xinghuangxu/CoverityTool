#!/usr/bin/perl

# NAME: Coverity-CoverageBatch.t
# SUMMARY: Test Module Coverity::CoverageBatch
# UPDATE DATE: Nov 17, 2014
# PROGRAMMER: Leon Xu (leonx@netapp.com)
#
# Copyright NetApp, Inc. 2014. All rights reserved.
#
#

use Test::More;
BEGIN { use_ok('Coverity::CoverageBatch') };
require_ok( 'Coverity::CoverageBatch' );

use File::Basename;
use Cwd 'abs_path';
use File::Compare;
my $absPath=abs_path($0);
my $dirname=dirname($absPath);

my $batchFileAns=$dirname.'/resource/answer.batch';
my $coverageConfigHash = {
	gcdaFolder=>$dirname.'/resource/gcda/Application',
	gcnoFolder=>$dirname.'/resource/gcno/Application',
	suitename=>'suiteSimple1',
	testname=>'testSimpleA',
	teststart=>'2014-10-14 14:07:21',
	output=>$dirname.'/temp/temp.batch'
};



my $coverageBatch=Coverity::CoverageBatch->new($coverageConfigHash);

ok($coverageBatch->gcdaFolder eq $coverageConfigHash->{gcdaFolder}, "get Coverage gcda Folder");
ok($coverageBatch->gcnoFolder eq $coverageConfigHash->{gcnoFolder}, "get Coverage gcno Folder");
ok($coverageBatch->suitename eq $coverageConfigHash->{suitename}, "get suitename");
ok($coverageBatch->testname eq $coverageConfigHash->{testname}, "get testname");
ok($coverageBatch->teststart eq $coverageConfigHash->{teststart}, "get teststart");
ok($coverageBatch->output eq $coverageConfigHash->{output}, "get output");

unlink($coverageConfigHash->{output});
$coverageBatch->create;  #generate batch file
ok(-e $coverageConfigHash->{output} != undef, "batch file created");
my $answerHandler=open(my $fh1, '<',$batchFileAns) or die("Cannot open batch file");
my $batchHandler=open(my $fh2, '<',$coverageConfigHash->{output}) or die("Cannot open batch file");
my $sameFile=1;
while(!eof($fh1) and !eof($fh2)){
	my $line1=<$fh1>;
	my $line2=<$fh2>;
	if(substr($line1,0,4) ne substr($line2,0,4)){
		#print $ansRow, $row;
		$sameFile=0;
		last;
	}
}
ok($sameFile==1, "batch file correct");

unlink($coverageConfigHash->{output});
done_testing( $number_of_tests_run );