CoverityTool
============

Before running tests:
	perl Makefile.PL
To execute all test:
	make test  
To execute a single test:
	perl -Mblib t/Coverity-CoverageBatch.t 
	

Usage:
There are two ways to use this package:

    Option 1. make install (Install this module to your load directory)
	
	Option 2. copyt lib and t folder into your src and add lib to your load folder
			use FindBin;                 # locate this script
			use lib "$FindBin::Bin/lib";  # use the lib directory
