/* Use exiftool to build a reference base of GPS tags from a set of images
	and write GPS tags to another set based on relative times between the
	two picture sets.
	
	i.e. Use your iPhone as a GPS track point device.
*/
/* TODO: IMG and DSCF times don't line up now in code
  DSCF1204 - May 23 3:40:45 PM
  IMG_0818 - May 23 3:40:41 PM
*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dsg/dsg.h>
#include <dsg/strbuf.h>
#include <dsg/dir.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xpath.h>
#include <libxml/xpathInternals.h>

/* GPS Track Points
*/
typedef struct TrkPt  {
	char *filename;
	char *lat;
	char *lat_ref;
	char *lon;
	char *lon_ref;
	char *alt;
	char *alt_ref;
	char *datetime;	/* localtime of camera DateTimeOriginal but prefer GPSDateTime*/
	/* next three based off of datetime string
	*/
	time_t gps_time;
	char *gps_datestamp;
	char *gps_timestamp;
} TrkPt;

typedef struct parseMap {
	xmlElementType type;		/* the type of the element/attr */
	char *name;					/* may be a path */
	int offset;
} parseMap;
static parseMap parse_map[] = {
#define offset(field) IniOffsetOf(TrkPt,field)
	{XML_ELEMENT_NODE, 		"trkpt",	(-1)},
	{XML_ELEMENT_NODE, 		"name",		offset(filename)},
	{XML_ELEMENT_NODE, 		"lat", 		offset(lat)},
	{XML_ELEMENT_NODE, 		"latref", 	offset(lat_ref)},
	{XML_ELEMENT_NODE, 		"lon", 		offset(lon)},
	{XML_ELEMENT_NODE, 		"lonref",	offset(lon_ref)},
	{XML_ELEMENT_NODE, 		"ele",		offset(alt)},
	{XML_ELEMENT_NODE, 		"eleref",	offset(alt_ref)},
	{XML_ELEMENT_NODE, 		"gpsdatetime",	offset(datetime)},
	{XML_ELEMENT_NODE, 		"createdate",		offset(datetime)},
#undef offset
};
enum {M_TRKPT, M_NAME, M_LAT, M_LAT_REF, M_LON, M_LON_REF, M_ALT, M_ALT_REF,
		M_GPSDATETIME, M_CREATEDATE};

typedef struct State {
	xmlDocPtr doc;
	xmlNode *root;
	xmlElementType last_type;
	xmlAttr *last_attr;
	xmlNode *last_elem;
	int nsp;
	
	char *gpx_path;
	APLIST trkpt_ap;
	TrkPt *cur_trkpt;
	
	char *o_reference_pic_dir;	/* to build GPS Track Points */
	char *o_dest_pic_path;	/* file or directory of pictures to add GPS info */
	char *o_tracklog;		/* file for trackfile */
	Boolean o_keep_tracklog;
	uint o_max_delta;		/* max seconds for the time interval on a match.
								i.e. maybe a TZ offset */
	int o_tzadj;			/* seconds from GMT for localtime values */
	struct {
		Boolean use;
		time_t begin;
		time_t end;
	} o_dest_include;		/* only include dest files within this range. */
	Boolean o_recurse;		/* traverse dest_pic or reference_pic paths */
	Boolean o_debug;
	
	uint n_updates;
} State;

static void parseElements(xmlNode * a_node, State *state);

#define EXIFTOOL_CMD	"exiftool"

/** Like bprintf() but returns an allocated string buffer.

        \return An allocated string that the caller must free.
*/
char *
mybsprintf(char *fmt, ...)
{
        char *s=NULL;
        va_list arg_ptr;

        va_start(arg_ptr, fmt);
        vasprintf(&s, fmt, arg_ptr); /* mallocs 's' */
        if(s==NULL)
                s=strdup("alloc failed");

        va_end(arg_ptr);

        return(s);
}

/* Returns
*/
static SuccFail
syscmd(State *state, char *cmd, char **output)
{
	SuccFail status=FAILED;
	FILE *fp;
	
	if(state->o_debug == True)
		fprintf(stderr, "RUNNING: %s\n", cmd);
		
	if((fp=popen(cmd, "r")) != NULL) {
		STRBUF *bp=bopen(512);
		char buf[1024];
		
		while(fgets(buf, sizeof(buf)-1, fp) != NULL)
			bstrcat(bp, buf);
		
		if(output)
			*output = bclosedup(bp);
		else
			bclose(bp);
			
		if(pclose(fp) == 0)
			status = SUCCESS;
	}
	return(status);
}

/* Call exiftool to create our GPX format for later call to 'exiftool -p'
*/
static char *
writeGpxFormat()
{
	FILE *fp;
	char *tmpgpxfmt;
	
	tmpgpxfmt = tmpnam(NULL);
	
	if((fp=fopen(tmpgpxfmt, "w")) != NULL) {
		fputs("\
#[HEAD]<?xml version=\"1.0\" encoding=\"utf-8\"?>\n\
#[HEAD]<mygpx version=\"1.0\"\n\
#[HEAD] creator=\"ExifTool $ExifToolVersion\"\n\
#[HEAD] xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\"\n\
#[HEAD] xmlns=\"http://www.topografix.com/GPX/1/0\"\n\
#[HEAD] xsi:schemaLocation=\"http://www.topografix.com/GPX/1/0 http://www.topografix.com/GPX/1/0/gpx.xsd\">\n\
#[HEAD]<trk>\n\
#[HEAD]<number>1</number>\n\
#[HEAD]<trkseg>\n\
#[BODY]<trkpt>\n\
#[BODY]  <name>$filename</name>\n\
#[BODY]  <lat>$gpslatitude#</lat>\n\
#[BODY]  <latref>$gpslatituderef#</latref>\n\
#[BODY]  <lon>$gpslongitude#</lon>\n\
#[BODY]  <lonref>$gpslongituderef#</lonref>\n\
#[BODY]  <ele>$gpsaltitude#</ele>\n\
#[BODY]  <eleref>$gpsaltituderef</eleref>\n\
",
				fp);

		/* Better is to use GPSDateTime (gpsdatetime) b/c it has TZ info but
		  the new iphone doesn't add this. Preference specified by this order.
		*/
		fputs("\
#[BODY]  <gpsdatetime>$gpsdatetime</gpsdatetime>\n\
#[BODY]  <createdate>$createdate</createdate>\n\
",
				fp);
		fputs("\
#[BODY]</trkpt>\n\
#[TAIL]</trkseg>\n\
#[TAIL]</trk>\n\
#[TAIL]</mygpx>\n\
",
			fp);
		fclose(fp);
	} else {
		perror(tmpgpxfmt);
		tmpgpxfmt=NULL;
	}

	return(tmpgpxfmt);
}

/* Returns an allocated string for the XML GPX output filename.
*/
static char *
createTrackLog(State *state)
{
	char *tmpgpx=NULL, *tmpgpxfmt;
	SuccFail status=FAILED;
	
	if((state->o_tracklog && access(state->o_tracklog, F_OK)!=0) ||
			state->o_tracklog==NULL) {
		fprintf(stdout, "Creating Track Points from Source Pictures\n");

		tmpgpx = state->o_tracklog?strdup(state->o_tracklog):strdup(tmpnam(NULL));
		if((tmpgpxfmt=writeGpxFormat()) != NULL) {
			char *cmd = mybsprintf("%s %s %s -p '%s' '%s' > %s",
				EXIFTOOL_CMD,
				(state->o_recurse==True ? "" : "-r"),
				(state->o_debug==True ? "" : "-ignoreMinorErrors"),
				tmpgpxfmt,
				state->o_reference_pic_dir, tmpgpx);
			if(syscmd(state, cmd, NULL) != 0) {
				fprintf(stderr, "TrackPointFile: creation failed\n");
				unlink(tmpgpx);
				free(tmpgpx);
				tmpgpx = NULL;
			}
			free(cmd);
			unlink(tmpgpxfmt);
		}
	} else {
		if(state->o_tracklog)
			fprintf(stderr, "WARNING: %s: track point file exists. Will not recreate.\n",
				state->o_tracklog);
		tmpgpx = strdup(state->o_tracklog);
	}
	
	return(tmpgpx);
}

static void
TrkPt_free(TrkPt *pt)
{
	if(pt) {
		IFF(pt->lat);
		IFF(pt->lat_ref);
		IFF(pt->lon);
		IFF(pt->lon_ref);
		IFF(pt->alt);
		IFF(pt->alt_ref);
		IFF(pt->datetime);
		IFF(pt->gps_datestamp);
		IFF(pt->gps_timestamp);
		free(pt);
	}
}

static void
addContent(xmlNode *node, State *state)
{
	int i;
	char **cpp, *cp;
	
	for(i=0; i<NumberOf(parse_map); ++i) {
		if(stricmp(state->last_elem->name, parse_map[i].name) == 0) {
			if(i == M_TRKPT) {
				TrkPt *trkpt;
				if(state->cur_trkpt &&
						(state->cur_trkpt->datetime==NULL ||
						 state->cur_trkpt->gps_time==(time_t)0)) {
					ap_destroy_items_pos(state->trkpt_ap, 1,
						ap_len(state->trkpt_ap)-1);
				}
				trkpt=(TrkPt *)zalloc(sizeof(TrkPt));
				ap_add_item(&state->trkpt_ap, trkpt);
				state->cur_trkpt = trkpt;
			} else if(parse_map[i].offset >= 0) {
				cpp = (char **)((char *)(state->cur_trkpt)+(parse_map[i].offset));
				/* make sure it has a value and not empty */
				for(cp=node->content; isspace(*cp); ++cp); /* trim leadingspace */
				if(*cp!='\0' && (*cpp == NULL)) {
					*cpp = strdup(node->content);
					if((i == M_GPSDATETIME || i==M_CREATEDATE) &&
								state->cur_trkpt->gps_time==(time_t)0) {
						/* 2011:05:23 11:51:52.86Z */
						struct tm tm;
						memset(&tm, 0, sizeof(tm));
						strptime(node->content, "%Y:%m:%d %H:%M:%S%z", &tm);
						tm.tm_isdst = (-1);
						state->cur_trkpt->gps_time = mktime(&tm);
						if(state->cur_trkpt->gps_time != (time_t)(-1)) {
								state->cur_trkpt->gps_datestamp = mybsprintf("%02d:%02d:%02d",
									tm.tm_year+1900, tm.tm_mon+1, tm.tm_mday);
								state->cur_trkpt->gps_timestamp = mybsprintf("%02d:%02d:%02d",
									tm.tm_hour, tm.tm_min, tm.tm_sec);
						} else { /* didn't parse a time */
								IFF(state->cur_trkpt->datetime);
								state->cur_trkpt->datetime = NULL;
								state->cur_trkpt->gps_time = (time_t)0;
						}
					}
				}
			}
			break;
		}
	}
}

static void
parseProperties(xmlAttr *attr, State *state)
{
    xmlAttr *cur_attr = NULL;

	state->nsp++;
    for (cur_attr = attr; cur_attr; cur_attr = cur_attr->next) {
		state->last_type=cur_attr->type;
		state->last_attr = cur_attr;
		if(cur_attr->children)
			parseElements(cur_attr->children, state);
	}
	
	state->nsp--;
}

static void
parseElements(xmlNode * a_node, State *state)
{
    xmlNode *cur_node = NULL;

	state->nsp++;
    for (cur_node = a_node; cur_node; cur_node = cur_node->next) {
        if (cur_node->type == XML_ELEMENT_NODE) {
			state->last_elem = cur_node;
        } else if(cur_node->type == XML_TEXT_NODE) {
			addContent(cur_node, state);
		}
		state->last_type = cur_node->type;
		if(cur_node->properties)
			parseProperties(cur_node->properties, state);

        parseElements(cur_node->children, state);
    }
	state->nsp--;
}

time_t
strToDate(char *str)
{
	time_t t=0;
	struct tm tm;
	
	memset(&tm, 0, sizeof(tm));
	strptime(str, "%Y:%m:%d %H:%M:%S", &tm);
	tm.tm_isdst=(-1);
	if((t=mktime(&tm)) == (time_t)(-1))
		t=0;
	
	return(t);
}

typedef struct ExifBaseInfo {
	time_t datetime;
	Boolean has_gps;
} ExifBaseInfo;

static SuccFail
exif_GetBaseInfo(State *state, char *path, ExifBaseInfo *infop)
{
    SuccFail status=FAILED;
	char *cmd, *output=NULL;
	time_t datetime=0;
	char *ep, *ele[] = {"Create Date", "GPS Latitude", NULL};
	
	memset(infop, 0, sizeof(ExifBaseInfo));
	infop->has_gps = False;

	cmd = mybsprintf("%s -CreateDate -GPSLatitude '%s'", EXIFTOOL_CMD, path);
	if((status=syscmd(state, cmd, &output)) == SUCCESS) {
		int i;
		for(i=0; ele[i]!=NULL; ++i) {
			if(i==0 && (ep=strstr(output, ele[i]))) {
				char *cp=strchr(ep, ':');
				if(cp != NULL) {
					++cp;
					while(isspace(*cp)) ++cp;
					infop->datetime = strToDate(cp);
					infop->datetime -= state->o_tzadj;
				}
			} else if(i==1 && (ep=strstr(output, ele[i]))) {
				infop->has_gps = True;
			}
		}
		IFF(output);
	}
	
	free(cmd);
	
	return(status);
}

static void
exif_UpdateGPS(State *state, char *path, TrkPt *trkpt)
{
	char *cmd;
	
	cmd = mybsprintf("%s -P -overwrite_original -GPSDateStamp='%s' -GPSTimeStamp='%s' \
-GPSLatitude='%s' \
-GPSLatitudeRef='%s' \
-GPSLongitude='%s' \
-GPSLongitudeRef='%s' \
-GPSAltitude='%s' \
-GPSAltitudeRef='%s' \
'%s'",
		EXIFTOOL_CMD,
		trkpt->gps_datestamp, trkpt->gps_timestamp,
		trkpt->lat,
		trkpt->lat_ref,
		trkpt->lon,
		trkpt->lon_ref,
		trkpt->alt,
		trkpt->alt_ref,
		path);
	if(syscmd(state, cmd, NULL) != 0)
		fprintf(stderr, "%s: Failed to update GPS info\n", path);
	else
		++state->n_updates;
}

/* Find TrkPt with the smallest delta before \a datetime
*/
static TrkPt *
locateTrackPoint(State *state, time_t datetime)
{
	TrkPt *pt_rtn=NULL, *pt;
	time_t lt=0;
	int i, delta=0;
	
	for(i=0; state->trkpt_ap[i]!=NULL; ++i) {
		pt = (TrkPt *)state->trkpt_ap[i];
		if(pt->gps_time == (time_t)0)
				continue;
		if((pt->gps_time < datetime) && (pt->gps_time>lt)) {
			pt_rtn = pt;
			delta = datetime - pt_rtn->gps_time;
		} else {
			if(pt_rtn) {
				int dprev, dthis;
				dprev = datetime - pt_rtn->gps_time;
				dthis = pt->gps_time - datetime;
				if(dthis < dprev) {
					pt_rtn = pt;
					delta = abs(pt->gps_time - datetime);
				}
			} else {
				/* try the next greater one
				*/
				pt_rtn = pt;
				delta = abs(pt_rtn->gps_time - datetime);
			}
			break;
		}
	}
	
	if(pt_rtn) {
		if((state->o_max_delta < delta))
			pt_rtn = NULL; /* too big of a delta */
	}
		
	return(pt_rtn);
}

static Boolean
direntCB(char *path, struct stat *sp, State *state)
{
	ExifBaseInfo exinfo;
	time_t datetime;
	
	sp=sp;
	
	if((sp->st_mode&S_IFMT) == S_IFDIR)
		goto done;
	exif_GetBaseInfo(state, path, &exinfo);
	if(exinfo.has_gps == False) {
		if(exinfo.datetime!=0) {
			TrkPt *trkpt;
			if(state->o_dest_include.use == True) {
				if((exinfo.datetime<state->o_dest_include.begin) ||
						(exinfo.datetime>state->o_dest_include.end)) {
					if(state->o_debug == True)
						fprintf(stderr, "Skipping: Range: %s\n",
							path);
					goto done;	/* out of range */
				}
			}
			if(state->o_debug == True)
				fprintf(stderr, "Examine: %s %s (%lu)\n", path, Ctime(&exinfo.datetime),
					exinfo.datetime);
			if((trkpt=locateTrackPoint(state, exinfo.datetime)) != NULL) {
				fprintf(stdout, " Update: from %s at %s (%lu) [delta=%d]\n",
					trkpt->filename,
					trkpt->datetime, trkpt->gps_time,
					exinfo.datetime-trkpt->gps_time);
				exif_UpdateGPS(state, path, trkpt);
			}
		}
	} else
		if(state->o_debug == True)
				fprintf(stderr, "Skipping: already has GPS info\n");
done:
	return(True);
}

static int
trkpt_sort(TrkPt *p1, TrkPt *p2)
{
	return(p1->gps_time - p2->gps_time);
}

static void
Usage(char *pgm)
{
	fprintf(stderr, "Usage: %s [-r] <-s src_directory [-t tracklog]> <-d dest_file_or_dir> [-z hours_from_gmt_on_pictures] [-m max_delta_secs] [-i date1 date2] [-D]\n\
  -s: Directory with pictures having GPS info as a tracking mechanism. If\n\
      program has been run before with (-t) option and source pictures have\n\
      not changed, then -s option is not necessary and will be ignored. This\n\
      will recursively examine files below the directory specified.\n\
  -d: The dest file/dir where GPS info is to be written based on -s locations\n\
  -r: Recursively traverse -s or -d path\n\
  -t: Specify the file to use for track points. If trackfile does not exist,\n\
      one will be dynamically created from the source pictures. If this\n\
      option is not specified, one is still created but deleted at exit.\n\
      The source (-s) option must be specified if the tracklog does not yet\n\
      exist.\n\
  -z: Specifies the number of hours from GMT when the pictures were taken.\n\
      For example, Kenya is GTM+3, so specify '3' for the argument.\n\
      This effectively tells us in what timezone the pictures were taken.\n\
      If you have pictures from multiple timezones, you should run this \n\
      program for each timezone and use the (-i) option to specify the range\n\
      for a particular timezone.\n\
  -m: the maximum number of seconds between gps time on src and \n\
      date-taken time on dest. Default is 30 minutes.\n\
  -i: Dates of pictures from dest to include (picture locatime). Needed \n\
      because date-taken has no timezone but gps_time is in GMT. Not \n\
      needed if all pictures from dest were taken in the same timezone.\n\
      Format for dates are YYYY:MM:DD HH:MM:SS\n\
  -D: Print debug info\n\
  \n\
  Examples:\n\
  1. Create trackpoint file from source pictures\n\
      geosync -t /tmp/foo.tkp -s /path/to/source/pictures\n\
  2. Update GPS tags on pictures from trackpoint file just created\n\
      geosync -t /tmp/foo.tkp -d /path/to/pictures/to/update\n",
		pgm);
}

int
main(int argc, char *argv[])
{
	State state;
	TrkPt *pt;
	char *tmpgpx;
	int i;
	
	memset(&state, 0, sizeof(state));
	state.o_max_delta = 30*60;	/* 30min default */
	
	if(argc<2) {
		Usage(argv[0]);
		exit(1);
	}
	for(i=1; i<argc; ++i) {
		if(strcmp(argv[i], "-s") == 0) {
			state.o_reference_pic_dir = argv[++i];
		} else if(strcmp(argv[i], "-d") == 0) {
			state.o_dest_pic_path = argv[++i];
		} else if(strcmp(argv[i], "-m") == 0) {
			state.o_max_delta = atoi(argv[++i]);
		} else if(strcmp(argv[i], "-z") == 0) {
			state.o_tzadj = atoi(argv[++i]) * (60 * 60); /* arg is hr, val is sec */
		} else if(strcmp(argv[i], "-t") == 0) {
			state.o_tracklog = argv[++i];
			state.o_keep_tracklog = True;
		} else if(strcmp(argv[i], "-i") == 0) {
			state.o_dest_include.use = True;
			state.o_dest_include.begin = strToDate(argv[++i]);
			state.o_dest_include.end = strToDate(argv[++i]);
		} else if(strcmp(argv[i], "-r") == 0) {
			state.o_recurse = True;
		} else if(strcmp(argv[i], "-D") == 0) {
			state.o_debug = True;
		} else {
			Usage(argv[0]);
			exit(1);
		}
	}
	
	if((tmpgpx=createTrackLog(&state)) == NULL)
		exit(1);
		
	state.doc = xmlParseFile(tmpgpx);
	
	if (state.doc == NULL) {
		return(1);
	}

	state.trkpt_ap = ap_create(25, (ApDestroyProc)TrkPt_free);
	state.root = xmlDocGetRootElement(state.doc);
	parseElements(state.root, &state);
	xmlFreeDoc(state.doc);
	
	if(state.o_keep_tracklog == False)
		unlink(tmpgpx);
	free(tmpgpx);

	ap_proc_sort(state.trkpt_ap, trkpt_sort);
	
	for(i=0; state.trkpt_ap[i]!=NULL; ++i) {
		pt = (TrkPt *)state.trkpt_ap[i];
		/* don't use points with latitude or latitude ref
		*/
		if(pt->lat==NULL || pt->lat_ref==NULL) {
			ap_destroy_items_pos(state.trkpt_ap, 1, i);
			--i;
		} else if(state.o_debug) {
			printf("\
GPSDateTime: %s (%lu) [%s]\n\
  GPSCoords: %s %s, %s %s, %sm %s\n",
				pt->datetime, pt->gps_time, pt->filename,
				pt->lat, pt->lat_ref, pt->lon, pt->lon_ref,
				pt->alt, pt->alt_ref);
		}
	}
	
	if(state.o_dest_pic_path) {
		DirTraverse(state.o_dest_pic_path,
			(DT_NODOTFILES | (state.o_recurse==True?DT_RECURSIVE:0)),
			direntCB, &state);
		printf("%s: Updated %d files\n", argv[0], state.n_updates);
	} else {
		fprintf(stderr, "WARNING: no files to modify. Use -d option.\n");
	}
	
	return(0);
}
