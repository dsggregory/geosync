#!/bin/sh

for f in *_original
do
	orig=`echo "$f" | sed -e 's/_original//'`
	mv "$f" "$orig"
done
