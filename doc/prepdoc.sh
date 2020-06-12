#! /bin/bash -x

# This is a short preparation script to create dummy
# files for running makeinfo if you want to work on
# the documentation without having a full build environment
# for pspp.
# You need to have makeinfo available on the shell which
# is provided by the texinfo package.
# Then you can create the html version of the documentation
# with:
#
# makeinfo --html pspp.texi
#
# This will create all hmtl files in the new directory pspp.
#
# Then you can view the documentation with:
#
# open pspp/index.html
#
# That will open your webbrowser and you can see the documentation

## Here are the three texi files which are normally generated during
## the build process.

# Create version.texi
echo "@set UPDATED 13 April 2020" > version.texi
echo "@set UPDATED-MONTH April 2020" >> version.texi
echo "@set EDITION 1.3.0-g90731b37a" >> version.texi
echo "@set VERSION 1.3.0-g90731b37a" >> version.texi

#Create tut.texi
echo "@set example-dir ../../examples" > tut.texi

#Create ni.texi
echo "@table @asis" > ni.texi
echo "@item @cmd{2SLS}" >> ni.texi
echo "This is just an example for missing items." >> ni.texi
echo "@end table" >> ni.texi


