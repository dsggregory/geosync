Overview
========
**Warning:** this is an example implementation. You will not be able to build as not all code required is available here and some is based on my private code.

Uses EXIFtool to read geo tags from a set of tracking photos and updates another set of photos geo tags based on relative timestamps.

Usage
========
```
Usage: ./geosync [-r] <-s src_directory [-t tracklog]> <-d dest_file_or_dir> [-z hours_from_gmt_on_pictures] [-m max_delta_secs] [-i date1 date2] [-D]
  -s: Directory with pictures having GPS info as a tracking mechanism. If
      program has been run before with (-t) option and source pictures have
      not changed, then -s option is not necessary and will be ignored. This
      will recursively examine files below the directory specified.
  -d: The dest file/dir where GPS info is to be written based on -s locations
  -r: Recursively traverse -s or -d path
  -t: Specify the file to use for track points. If trackfile does not exist,
      one will be dynamically created from the source pictures. If this
      option is not specified, one is still created but deleted at exit.
      The source (-s) option must be specified if the tracklog does not yet
      exist.
  -z: Specifies the number of hours from GMT when the pictures were taken.
      For example, Kenya is GTM+3, so specify '3' for the argument.
      This effectively tells us in what timezone the pictures were taken.
      If you have pictures from multiple timezones, you should run this 
      program for each timezone and use the (-i) option to specify the range
      for a particular timezone.
  -m: the maximum number of seconds between gps time on src and 
      date-taken time on dest. Default is 30 minutes.
  -i: Dates of pictures from dest to include (picture locatime). Needed 
      because date-taken has no timezone but gps_time is in GMT. Not 
      needed if all pictures from dest were taken in the same timezone.
      Format for dates are YYYY:MM:DD HH:MM:SS
  -D: Print debug info
  
  Examples:
  1. Create trackpoint file from source pictures
      geosync -t /tmp/foo.tkp -s /path/to/source/pictures
  2. Update GPS tags on pictures from trackpoint file just created
      geosync -t /tmp/foo.tkp -d /path/to/pictures/to/update
```
