#!/usr/bin/env @PYTHONPROG@

"""
Perform a coherent isolated pulsar search over a narrow frequency band
on a set of sky patches, using a GRID-enabled system.
"""
# Constants
versionN = 1.0
sftList = "sftList.txt"
sftPath = "sftPath.txt"
dagname = "ClusterComputeF.dag"

import sys
import os
import time
import getopt
from ClusterComputeFUtils import *
from pipeline import *

def usage():
  """
  Print a usage message to stdout.
  """
  msg = """\
NAME
       ClusterComputeF

SYNOPSIS
       ClusterComputeF [options]... listfile

DESCRIPTION
       Constructs and submits a DAGMan job to Condor that performs a
       coherent pulsar search on a set of sky patches.  The sky
       patches are listed in a file specified by the command-line
       argument, while other parameters of the search are specified by
       the command line options.

       The sky patch list file consists of any number of lines
       specifying N-vertex polygons (alpha,delta) in right ascension
       and declination, where each line is of the form:

              -R (alpha1,delta1),...,(alphaN,deltaN)

       The number of vertecies N may vary from line to line. No
       whitespace is permitted within or between coordinate pairs.
       Alternatively, lines may specify patches by a position
       (alpha,delta) and interval (Dalpha,Ddelta) in right ascension
       and declination, by taking the form:

              -a alpha -d delta -z Dalpha -c Ddelta

       Lines of each form may be interspersed within a listfile.  For
       a given DAGMan job, each sky patch is uniquely identified by
       its line number within the listfile, and will be analyzed as a
       separate Condor job.

       The other command-line options permitted [and their defaults]
       are:

       -h, --help
              print this help message

       -s, --start=START
              start at GPS time START (seconds) [0]

       -e, --end=END
              end at GPS time END (seconds) [86400]

       -i, --instrument=IFO
              specify detector (H1, H2, L1 or G) [H1]

       -f, --frequency=FREQ
              start at frequency FREQ (Hertz) [1200.0]

       -b, --bandwidth=BAND
              cover frequency bandwidth BAND (Hertz) [1.0]

       -d, --spindown=SPIN
	      search up from SPIN spindown (Hertz^2) [0.0]

       -p, --spinband=SBAND
	      search up to SPIN+SBAND (Hertz^2) [0.0]

       -m, --metric=CODE
	      specify metric computation method [1]:
	             0 = none
		     1 = PtoleMetric
		     2 = CoherentMetric

       -x, --mismatch=MAX
	      set maximum mismatch to MAX [0.02]

       -t, --threshold=THRESH
	      set F-statistic threshold to THRESH [10.0]

       -l, --liststart=LSTART
	      skip first LSTART patches in list [0]

       -n, --num=NUM
	      search NUM patches from list (negative means search all
	      remaining patches) [-1]

       -r, --rls-server=SERVER
              specify name of RLS server for collecting data

       -c, --calibration=TYPE
              select calibration type for input data [Funky]

       -v, --calibration-version=VERSION
              select version number for calibration [3]

       -V, --version
              print version number of this script to stderr and exit


       Any input SFT data not present on the cluster will be placed in
       the cluster shared directory, which will be determined
       automatically (and printed to the terminal when this script is
       run).  Output files will be written in a subdirectory of this
       directory, named ClusterComputeF_%010i, where the extension is
       a ten-digit UNIX time at job submission (used to ensure the
       directory name is unique).  This directory will contain a copy
       of the listfile of sky regions, a file named "command" storing
       the command line used to invoke this script, another named
       "version" storing the version number of this script, and a
       narrowband SFT file with a name of the form:

         %s-SFT_%8.3f_%8.3f-%010i-%08i.gwf

       where the fields are, in order, the IFO string (e.g. "H1"), the
       starting frequency (Hz), the bandwidth (Hz), the starting GPS
       time (s), and the duration (s).  Output files generated by
       FrComputeFStatistic will be prefixed by a 6-digit node number
       corresponding to a line number in the listfile.

"""
  print msg

# Options and their defaults
shortopts = "hs:e:i:f:b:d:p:m:x:t:l:n:r:c:v:V"
longopts = [ "help", "start=", "end=", "instrument=", "frequency=",
             "bandwidth=", "spindown=", "spinband=", "metric=",
             "mismatch=", "threshold=", "liststart=", "num=",
             "rls-server=", "calibration=", "calibration-version=",
             "version" ]
start = 0
end = 86400
instrument = "H1"
frequency = 1200.0
bandwidth = 1.0
spindown  = 0.0
spinband  = 0.0
metric = 1
mismatch = 0.02
threshold = 10.0
liststart = 0
num = -1
rls_server = "rls://hydra.phys.uwm.edu"
calibration = "Funky"
calibration_version = 3

# Parse command line
try:
  options, args = getopt.getopt( sys.argv[1:], shortopts, longopts )
except getopt.GetoptError:
  print >>sys.stderr, sys.argv[0] + ": Error parsing command line"
  print >>sys.stderr, "Use " + sys.argv[0] + " --help for usage"
  sys.exit( 2 )

for opt, value in options:
  if opt in ( "-h", "--help" ):
    usage()
    sys.exit( 0 )
  elif opt in ( "-s", "--start" ):
    start = long( value )
  elif opt in ( "-e", "--end" ):
    end = long( value )
  elif opt in ( "-i", "--instrument" ):
    instrument =  value 
  elif opt in ( "-f", "--frequency" ):
    frequency = float( value )
  elif opt in ( "-b", "--bandwidth" ):
    bandwidth = float( value )
  elif opt in ( "-d", "--spindown" ):
    spindown = float( value )
  elif opt in ( "-p", "--spinband" ):
    spinband = float( value )
  elif opt in ( "-m", "--metric" ):
    metric = int( value )
  elif opt in ( "-x", "--mismatch" ):
    mismatch = float( value )
  elif opt in ( "-t", "--threshold" ):
    threshold = float( value )
  elif opt in ( "-l", "--liststart" ):
    liststart = int( value )
  elif opt in ( "-n", "--num" ):
    num = int( value )
  elif opt in ( "-c", "--calibration" ):
    calibration = value
  elif opt in  ("-r", "--rls-server" ):
     rls_server = value
  elif opt in ( "-v", "--calibration-version" ):
    calibration_version = int( value )
  elif opt in ( "-V", "--version" ):
    print >>sys.stderr, versionN
    sys.exit( 0 )
  else:
    print >>sys.stderr, sys.argv[0] + ": Unrecognized option " + opt
    print >>sys.stderr, "Use " + sys.argv[0] + " --help for usage"
    sys.exit( 2 )

if len( args ) < 1:
  print >>sys.stderr, sys.argv[0] + ": Missing argument"
  print >>sys.stderr, "Use " + sys.argv[0] + " --help for usage"
  sys.exit( 2 )


# Get interferometer code from name
if instrument == "H1" or instrument == "H2":
  ifocode = 2
elif instrument == "L1":
  ifocode = 1
elif instrument == "G":
  ifocode = 0
else:
  print >>sys.stderr, sys.argv[0] + " - unrecognized instrument " + \
        instrument
  sys.exit( 1 )

### Get name of cluster shared directory and output directory.
sharedDir = "."
outputDir = sharedDir + "/ClusterComputeF_%010i/" % int( time.time() )
nbsftname = outputDir + instrument + "-SFT_%08.3f_%08.3f-%010i-%08i.gwf" % \
            ( frequency, bandwidth, start, end - start )
os.mkdir( outputDir )
fp = open( outputDir + "command", "w" )
fp.write( " ".join( sys.argv ) + "\n" )
fp.close()
fp = open( outputDir + "version", "w" )
fp.write( str( versionN ) + "\n" )
fp.close()

### Get name of directory where ephemerides are stored, and year of ephemeris
ephemDir = "."
year = getyearstr( ephemDir, start, end )
if year == "":
  print >>sys.stderr, sys.argv[0] + " - no ephemerides for GPS " \
        "times %i-%i" % ( start, end )
  sys.exit(1)

# Get list of sky patches from input file
patches = skyPatches()
try:
  patches.read( args[0], liststart, num )
except EOFError:
  print >>sys.stderr, sys.argv[0] + " - only %i patches read " \
        "(%i requested) -- continuing anyway" % \
        ( len( patches.list ), num )
try:
  patches.check()
except Warning, inst:
  print >>sys.stderr, str( inst )
num = len( patches.list )

# Create SFT search job.
searchJob = CondorDAGJob( "vanilla", "lalapps_QueryMetadataLFN" )
searchJob.set_sub_file(                "QueryMetadataLFN.sub" )
searchJob.set_stdout_file( outputDir + "QueryMetadataLFN.out" )
searchJob.set_stderr_file( outputDir + "QueryMetadataLFN.err" )
searchJob.add_opt( "calibration", calibration )
searchJob.add_opt( "calibration-version", "%i" % calibration_version )
searchJob.add_opt( "instrument", instrument )
searchJob.add_opt( "gps-start-time", "%i" % start )
searchJob.add_opt( "gps-end-time",   "%i" % end )
searchJob.add_opt( "output", outputDir + sftList )

# Create SFT collection job.
collectJob = CondorDAGJob( "vanilla", "lalapps_GatherLFN" )
collectJob.set_sub_file(                "GatherLFN.sub" )
collectJob.set_stdout_file( outputDir + "GatherLFN.out" )
collectJob.set_stderr_file( outputDir + "GatherLFN.err" )
collectJob.add_opt( "input",  outputDir + sftList )
collectJob.add_opt( "output", outputDir + sftPath )
collectJob.add_opt( "server", rls_server )
collectJob.add_opt( "bucket", sharedDir )

# Create SFT extraction job
extractJob = CondorDAGJob( "standard",  "narrowBandExtract" )
extractJob.set_sub_file(                "narrowBandExtract.sub" )
extractJob.set_stdout_file( outputDir + "narrowBandExtract.out" )
extractJob.set_stderr_file( outputDir + "narrowBandExtract.err" )
extractJob.add_opt( "frequency", "%f" % frequency )
extractJob.add_opt( "bandwidth", "%f" % bandwidth )
extractJob.add_opt( "input",  outputDir + sftPath )
extractJob.add_opt( "output", nbsftname )

# Create FrComputeFStatistic job
computeJob = CondorDAGJob( "standard",   "FrComputeFStatistic" )
computeJob.set_sub_file(                 "FrComputeFStatistic.sub" )
computeJob.set_stdout_file( outputDir + "$(node).out" )
computeJob.set_stderr_file( outputDir + "$(node).err" )
computeJob.add_short_opt( "I", "%i" % ifocode )
computeJob.add_short_opt( "f", "%f" % frequency )
computeJob.add_short_opt( "b", "%f" % bandwidth )
computeJob.add_short_opt( "s", "%f" % spindown )
computeJob.add_short_opt( "m", "%f" % spinband )
computeJob.add_short_opt( "M", "%i" % metric )
computeJob.add_short_opt( "X", "%f" % mismatch )
computeJob.add_short_opt( "F", "%f" % threshold )
computeJob.add_short_opt( "i", nbsftname )
computeJob.add_short_opt( "E", ephemDir )
computeJob.add_short_opt( "y", year )
computeJob.add_short_opt( "o", outputDir + "$(node)_" )

# Create ClusterComputeF DAG
dag = CondorDAG( "ClusterComputeF.log" )
dag.set_dag_file( dagname )
searchNode = CondorDAGNode( searchJob )
searchNode.set_pre_script( "/bin/cp" )
searchNode.add_pre_script_arg( args[0] )
searchNode.add_pre_script_arg( outputDir )
dag.add_node( searchNode )
collectNode = CondorDAGNode( collectJob )
collectNode.add_parent( searchNode )
dag.add_node( collectNode )
extractNode = CondorDAGNode( extractJob )
extractNode.add_parent( collectNode )
dag.add_node( extractNode )
for i in range( 0, num ):
  computeNode =  CondorDAGNode( computeJob )
  computeNode.add_macro( "node", "%06i" % ( i + liststart ) )
  computeNode.add_var_arg( patches.list[i] )
  computeNode.add_parent( extractNode )
  # finalizeNode.add_parent( computeNode )
  dag.add_node( computeNode )

# Execute Condor job
del patches
dag.write_sub_files()
dag.write_dag()
print sys.argv[0] + " - Submitting jobs %i through %i" % \
      ( liststart, liststart + num - 1 )
print "\t Shared directory: " + sharedDir
print "\t Output directory: " + outputDir
#execlp( "condor_submit_dag", dagname )
