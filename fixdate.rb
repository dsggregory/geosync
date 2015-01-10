#!/usr/bin/env ruby
#
# Adjust dates on pictures
# Camera was april, am instead of pm, +7 hours
# Need to:
#  o Add one month
#  o Add 12 hours
#  o Add two minutes
# TODO: get time_t and add
# Apr 19 9:20:51pm

#
# exiftool -s -j -d '%Y/%m/%d %H:%M:%S %z' ../20140525-171456/IMG_5370.JPG
# exiftool -overwrite_original_in_place -DateTimeOriginal=%s -CreateDate=%s -ModifyDate=%s
#  "DateTimeOriginal": "2014:04:24 21:33:10",
#  "CreateDate": "2014:04:24 21:33:10",
#  "ModifyDate": "2014:04:24 21:33:10",

require 'date'
require 'json'

ARGV.each do |fn|
  jd = `exiftool -j -d '%Y-%m-%dT%H:%M:%S %z' #{fn}`
  j = JSON.parse(jd)
  d = DateTime.parse(j[0]['DateTimeOriginal'])
  mday = d.mday
  if(d.hour>12)
	mday += 1
	hour = (d.hour+12)-24
  else
	hour = d.hour+12
  end
  ds = sprintf("%02d:%02d:%02d %02d:%02d:%02d", d.year, d.month+1, mday, hour, d.min+2, d.sec)
  puts "#{fn}: #{d} -> #{ds}"
  system('exiftool', '-overwrite_original_in_place', "-DateTimeOriginal=#{ds}",
		"-CreateDate=#{ds}", "-ModifyDate=#{ds}", "#{fn}")
end
