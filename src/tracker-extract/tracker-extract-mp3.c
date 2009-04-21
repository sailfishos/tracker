/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2006, Mr Jamie McCracken (jamiemcc@gnome.org)
 * Copyright (C) 2008, Nokia
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 * * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 */

#include "config.h"

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>

#include <glib.h>
#include <glib/gstdio.h>

#ifndef G_OS_WIN32
#include <sys/mman.h>
#endif /* G_OS_WIN32 */

#include <libtracker-common/tracker-file-utils.h>
#include <libtracker-common/tracker-statement-list.h>
#include <libtracker-common/tracker-ontology.h>
#include <libtracker-common/tracker-utils.h>

#include "tracker-main.h"
#include "tracker-extract-albumart.h"

/* We mmap the beginning of the file and read separately the last 128 bytes
   for id3v1 tags. While these are probably cornercases the rationale is that
   we don't want to fault a whole page for the last 128 bytes and on the other
   we don't want to mmap the whole file with unlimited size (might need to create
   private copy in some special cases, finding continuous space etc). We now take
   5 first MB of the file and assume that this is enough. In theory there is no
   maximum size as someone could embed 50 gigabytes of albumart there.
*/

#define MAX_FILE_READ	  1024 * 1024 * 5
#define MAX_MP3_SCAN_DEEP 16768

#define MAX_FRAMES_SCAN   512
#define VBR_THRESHOLD     16

#define ID3V1_SIZE        128

#define NMM_PREFIX TRACKER_NMM_PREFIX
#define NFO_PREFIX TRACKER_NFO_PREFIX
#define NIE_PREFIX TRACKER_NIE_PREFIX
#define DC_PREFIX TRACKER_DC_PREFIX
#define NCO_PREFIX TRACKER_NCO_PREFIX

#define RDF_PREFIX TRACKER_RDF_PREFIX
#define RDF_TYPE RDF_PREFIX "type"

typedef struct {
	const gchar *text;
	const gchar *type;
	const gchar *urn;
	const gchar *rdf_type;
	const gchar *predicate;
} Matches;

typedef struct {
	gchar *title;
	gchar *artist;
	gchar *album;
	gchar *year;
	gchar *comment;
	gchar *trackno;
	gchar *genre;
} id3tag;

typedef struct {
	size_t         size;
	size_t         id3v2_size;

	guint32        duration;

	unsigned char *albumartdata;
	size_t         albumartsize;
} file_data;

enum {
	MPEG_ERR,
	MPEG_V1,
	MPEG_V2,
	MPEG_V25
};

enum {
	LAYER_ERR,
	LAYER_1,
	LAYER_2,
	LAYER_3
};

static void extract_mp3 (const gchar *filename,
			 GPtrArray  *metadata);

static const char *const genre_names[] = {
	"Blues",
	"Classic Rock",
	"Country",
	"Dance",
	"Disco",
	"Funk",
	"Grunge",
	"Hip-Hop",
	"Jazz",
	"Metal",
	"New Age",
	"Oldies",
	"Other",
	"Pop",
	"R&B",
	"Rap",
	"Reggae",
	"Rock",
	"Techno",
	"Industrial",
	"Alternative",
	"Ska",
	"Death Metal",
	"Pranks",
	"Soundtrack",
	"Euro-Techno",
	"Ambient",
	"Trip-Hop",
	"Vocal",
	"Jazz+Funk",
	"Fusion",
	"Trance",
	"Classical",
	"Instrumental",
	"Acid",
	"House",
	"Game",
	"Sound Clip",
	"Gospel",
	"Noise",
	"Alt. Rock",
	"Bass",
	"Soul",
	"Punk",
	"Space",
	"Meditative",
	"Instrumental Pop",
	"Instrumental Rock",
	"Ethnic",
	"Gothic",
	"Darkwave",
	"Techno-Industrial",
	"Electronic",
	"Pop-Folk",
	"Eurodance",
	"Dream",
	"Southern Rock",
	"Comedy",
	"Cult",
	"Gangsta Rap",
	"Top 40",
	"Christian Rap",
	"Pop/Funk",
	"Jungle",
	"Native American",
	"Cabaret",
	"New Wave",
	"Psychedelic",
	"Rave",
	"Showtunes",
	"Trailer",
	"Lo-Fi",
	"Tribal",
	"Acid Punk",
	"Acid Jazz",
	"Polka",
	"Retro",
	"Musical",
	"Rock & Roll",
	"Hard Rock",
	"Folk",
	"Folk/Rock",
	"National Folk",
	"Swing",
	"Fast-Fusion",
	"Bebob",
	"Latin",
	"Revival",
	"Celtic",
	"Bluegrass",
	"Avantgarde",
	"Gothic Rock",
	"Progressive Rock",
	"Psychedelic Rock",
	"Symphonic Rock",
	"Slow Rock",
	"Big Band",
	"Chorus",
	"Easy Listening",
	"Acoustic",
	"Humour",
	"Speech",
	"Chanson",
	"Opera",
	"Chamber Music",
	"Sonata",
	"Symphony",
	"Booty Bass",
	"Primus",
	"Porn Groove",
	"Satire",
	"Slow Jam",
	"Club",
	"Tango",
	"Samba",
	"Folklore",
	"Ballad",
	"Power Ballad",
	"Rhythmic Soul",
	"Freestyle",
	"Duet",
	"Punk Rock",
	"Drum Solo",
	"A Cappella",
	"Euro-House",
	"Dance Hall",
	"Goa",
	"Drum & Bass",
	"Club-House",
	"Hardcore",
	"Terror",
	"Indie",
	"BritPop",
	"Negerpunk",
	"Polsk Punk",
	"Beat",
	"Christian Gangsta Rap",
	"Heavy Metal",
	"Black Metal",
	"Crossover",
	"Contemporary Christian",
	"Christian Rock",
	"Merengue",
	"Salsa",
	"Thrash Metal",
	"Anime",
	"JPop",
	"Synthpop"
};

static const guint sync_mask = 0xE0FF;
static const guint mpeg_ver_mask = 0x1800;
static const guint mpeg_layer_mask = 0x600;
static const guint bitrate_mask = 0xF00000;
static const guint freq_mask = 0xC0000;
static const guint ch_mask = 0xC0000000;
static const guint pad_mask = 0x20000;

static guint bitrate_table[16][6] = {
	{   0,   0,   0,   0,   0,   0 },
	{  32,  32,  32,  32,  32,   8 },
	{  64,  48,  40,  64,  48,  16 },
	{  96,  56,  48,  96,  56,  24 },
	{ 128,  64,  56, 128,  64,  32 },
	{ 160,  80,  64, 160,  80,  64 },
	{ 192,  96,  80, 192,  96,  80 },
	{ 224, 112,  96, 224, 112,  56 },
	{ 256, 128, 112, 256, 128,  64 },
	{ 288, 160, 128, 288, 160, 128 },
	{ 320, 192, 160, 320, 192, 160 },
	{ 352, 224, 192, 352, 224, 112 },
	{ 384, 256, 224, 384, 256, 128 },
	{ 416, 320, 256, 416, 320, 256 },
	{ 448, 384, 320, 448, 384, 320 },
	{  -1,  -1,  -1,  -1,  -1,  -1 }
};

static gint freq_table[4][3] = {
	{44100, 22050, 11025},
	{48000, 24000, 12000},
	{32000, 16000, 8000}
};

static TrackerExtractData extract_data[] = {
	{ "audio/mpeg", extract_mp3 },
	{ "audio/x-mp3", extract_mp3 },
	{ NULL, NULL }
};

static char *
read_id3v1_buffer (int fd, goffset size)
{
	char *buffer;
	guint bytes_read;
	guint rc;

	buffer = g_malloc (ID3V1_SIZE);

	if (!buffer) {
		return NULL;
	}

	if (lseek (fd, size-ID3V1_SIZE, SEEK_SET) < 0) {
		g_free (buffer);
		return NULL;
	}

	bytes_read = 0;
	
	while (bytes_read < ID3V1_SIZE) {
		rc = read(fd,
			  buffer + bytes_read,
			  ID3V1_SIZE - bytes_read);
		if (rc == -1) {
			if (errno != EINTR) {
				g_free (buffer);
				return NULL;
			}
		}
		else if (rc == 0)
			break;
		else
			bytes_read += rc;
	}
	
	return buffer;
}

/* Convert from UCS-2 to UTF-8 checking the BOM.*/
static gchar *
ucs2_to_utf8(const gchar *data, guint len)
{
        const gchar   *encoding = NULL;
        guint16  c;
	gboolean be;
        gchar   *utf8 = NULL;

        memcpy (&c, data, 2);

	switch (c) {
        case 0xfeff:
        case 0xfffe:
		be = (G_BYTE_ORDER == G_BIG_ENDIAN);
		be = (c == 0xfeff) ? be : !be;
		encoding = be ? "UCS-2BE" : "UCS-2LE";
                data += 2;
                len -= 2;
                break;
        default:
                encoding = "UCS-2";
                break;
        }

        utf8 = g_convert (data, len, "UTF-8", encoding, NULL, NULL, NULL);

        return utf8;
}

/* Get the genre codes from regular expressions */
static gboolean
get_genre_number (const char *str, guint *genre)
{
	static GRegex *regex1 = NULL;
	static GRegex *regex2 = NULL;
	GMatchInfo *info = NULL;
	gchar *result = NULL;

	if (!regex1)
		regex1 = g_regex_new ("\\(([0-9]+)\\)", 0, 0, NULL);

	if (!regex2)
		regex2 = g_regex_new ("([0-9]+)\\z", 0, 0, NULL);

	if (g_regex_match (regex1, str, 0, &info)) {
		result = g_match_info_fetch (info, 1);
		if (result) {
			*genre = atoi (result);
			g_free (result);
			g_match_info_free (info);
			return TRUE;
		}
	}

	g_match_info_free (info);

	if (g_regex_match (regex2, str, 0, &info)) {
		result = g_match_info_fetch (info, 1);
		if (result) {
			*genre = atoi (result);
			g_free (result);
			g_match_info_free (info);
			return TRUE;
		}	
	}

	g_match_info_free (info);

	return FALSE;
}

static const gchar *
get_genre_name (guint number)
{
	if (number > G_N_ELEMENTS (genre_names)) {
		return NULL;
	}

	return genre_names[number];
}

static void
un_unsync (const unsigned char *source,
	   size_t               size,
	   unsigned char      **destination,
	   size_t              *dest_size)
{
	size_t   offset  = 0;
	gchar   *dest;
	size_t   new_size;

	*destination = g_malloc0 (size);
	dest         = *destination;
	new_size     = size;

	while (offset < size) {
		*dest = source[offset];

		if ((source[offset] == 0xFF) && 
		    (source[offset+1] == 0x00)) {
			offset++;
			new_size--;
		}
		dest++;
		offset++;
	}
	
	*dest_size = new_size;
}

static gboolean
get_id3 (const gchar *data,
	 size_t       size,
	 id3tag      *id3)
{
	const gchar *pos;
	gchar buf[5];

	if (size < 128) {
		return FALSE;
	}

	pos = &data[size - 128];

	if (strncmp ("TAG", pos, 3) != 0) {
		return FALSE;
	}

	pos += 3;

	id3->title = g_convert (pos, 30,
				"UTF-8",
				"ISO-8859-1",
				NULL, NULL, NULL);

	pos += 30;
	id3->artist = g_convert (pos, 30,
				 "UTF-8",
				 "ISO-8859-1",
				 NULL, NULL, NULL);
	pos += 30;
	id3->album = g_convert (pos, 30,
				"UTF-8",
				"ISO-8859-1",
				NULL, NULL, NULL);
	pos += 30;
	id3->year = g_convert (pos, 4,
			       "UTF-8",
			       "ISO-8859-1",
			       NULL, NULL, NULL);

	pos += 4;

	if (pos[28] != (guint)0) {
		id3->comment = g_convert (pos, 30,
					  "UTF-8",
					  "ISO-8859-1",
					  NULL, NULL, NULL);

		id3->trackno = NULL;
	} else {
		id3->comment = g_convert (pos, 28,
					  "UTF-8",
					  "ISO-8859-1",
					  NULL, NULL, NULL);
		snprintf (buf, 5, "%d", pos[29]);
		id3->trackno = strdup(buf);
	}

	pos += 30;

	id3->genre = g_strdup (get_genre_name ((guint) pos[0]));

	if (!id3->genre) {
		id3->genre = g_strdup ("");
	}

	return TRUE;
}

static gboolean
mp3_parse_header (const gchar *data,
		  size_t       size,
		  size_t       seek_pos,
		  const gchar *uri,
		  GPtrArray   *metadata,
		  file_data   *filedata)
{
	guint header;
	gchar mpeg_ver = 0;
	gchar layer_ver = 0;
	guint spfp8 = 0;
	guint padsize = 0;
	gint idx_num = 0;
	guint bitrate = 0;
	guint avg_bps = 0;
	gint vbr_flag = 0;
	guint length = 0;
	guint sample_rate = 0;
	gint ch = 0;
	guint frame_size;
	guint frames = 0;
	size_t pos = 0;

	pos = seek_pos;

	memcpy (&header, &data[pos], sizeof (header));

	switch (header & mpeg_ver_mask) {
	    case 0x800:
		    mpeg_ver = MPEG_ERR;
		    break;
	    case 0x1000:
		    tracker_statement_list_insert (metadata, uri,
					 NFO_PREFIX "codec",
					 "MPEG");
/*		    tracker_statement_list_insert (metadata, uri,
					 "Audio:CodecVersion",
					 "2");*/
		    mpeg_ver = MPEG_V2;
		    spfp8 = 72;
		    break;
	    case 0x1800:
		    tracker_statement_list_insert (metadata, uri,
					 NFO_PREFIX "codec",
					 "MPEG");
/*		    tracker_statement_list_insert (metadata, uri,
					 "Audio:CodecVersion",
					 "1");*/
		    mpeg_ver = MPEG_V1;
		    spfp8 = 144;
		    break;
	    case 0:
		    tracker_statement_list_insert (metadata, uri,
					 NFO_PREFIX "codec",
					 "MPEG");
/*		    tracker_statement_list_insert (metadata, uri,
					 "Audio:CodecVersion",
					 "2.5");*/
		    mpeg_ver = MPEG_V25;
		    spfp8 = 72;
		    break;
	    default:
		    break;
	}

	switch (header & mpeg_layer_mask) {
	    case 0x400:
		    layer_ver = LAYER_2;
		    padsize = 1;
		    break;
	    case 0x200:
		    layer_ver = LAYER_3;
		    padsize = 1;
		    break;
	    case 0x600:
		    layer_ver = LAYER_1;
		    padsize = 4;
		    break;
	    case 0:
		    layer_ver = LAYER_ERR;
	    default:
		    break;
	}

	if (!layer_ver || !mpeg_ver) {
		/* g_debug ("Unknown mpeg type: %d, %d", mpeg_ver, layer_ver); */
		/* Unknown mpeg type */
		return FALSE;
	}
	
	if (mpeg_ver<3) {
		idx_num = (mpeg_ver - 1) * 3 + layer_ver - 1;
	} else {
		idx_num = 2 + layer_ver;
	}
	
	if ((header & ch_mask) == ch_mask) {
		ch = 1;
		tracker_statement_list_insert (metadata, uri,
				     NFO_PREFIX "channels",
				     "1");
	} else {
		ch=2; /*stereo non stereo select*/
		tracker_statement_list_insert (metadata, uri,
				     NFO_PREFIX "channels",
				     "2");
	}
	
	/* We assume mpeg version, layer and channels are constant in frames */
	do {
		frames++;
		bitrate = 1000 * bitrate_table[(header & bitrate_mask) >> 20][idx_num];

		if (bitrate <= 0) {
			frames--;
			return FALSE;
		}

		sample_rate = freq_table[(header & freq_mask) >> 18][mpeg_ver - 1];
		/* Whoever wrote this check: it's pointless, sample_rate is a uint,
		 so it can't ever be < 0. Hence commenting it out (pvanhoof)
		 if (sample_rate < 0) {
			* Error in header *
			frames--;
			return FALSE;
		}*/

		frame_size = spfp8 * bitrate / (sample_rate ? sample_rate : 1) + padsize*((header & pad_mask) >> 17);
		avg_bps += bitrate / 1000;

		pos += frame_size;

		if (frames > MAX_FRAMES_SCAN) {
			/* Optimization */
			break;
		}

		if (avg_bps / frames != bitrate / 1000) {
			vbr_flag = 1;
		}

		if (pos + sizeof (header) > size) {
			/* EOF */
			break;
		}

		if ((!vbr_flag) && (frames > VBR_THRESHOLD)) {
			break;
		}

		memcpy(&header, &data[pos], sizeof (header));
	} while ((header & sync_mask) == sync_mask);

	if (frames < 2) { /* At least 2 frames to check the right position */
		/* No valid frames */
		return FALSE;
	}

	avg_bps /= frames;

	if (filedata->duration==0) {
		if ((!vbr_flag || frames > VBR_THRESHOLD) || (frames > MAX_FRAMES_SCAN)) {
			/* If not all frames scanned */
			length = (filedata->size - filedata->id3v2_size) / (avg_bps ? avg_bps : bitrate ? bitrate : 0xFFFFFFFF) / 125;
		} else{
			length = 1152 * frames / (sample_rate ? sample_rate : 0xFFFFFFFF);
		}
 
		tracker_statement_list_insert_with_int (metadata, uri,
				     NMM_PREFIX "length",
				     length);
	}

	tracker_statement_list_insert_with_int (metadata, uri,
			     NFO_PREFIX "sampleRate",
			     sample_rate);
	tracker_statement_list_insert_with_int (metadata, uri,
			     NFO_PREFIX "averageBitrate",
			     avg_bps*1000);

	return TRUE;
}

static void
mp3_parse (const gchar *data,
	   size_t       size,
	   size_t       offset,
	   const gchar *uri,
	   GPtrArray  *metadata,
	   file_data   *filedata)
{
	guint header;
	guint counter = 0;
	guint pos = offset;

	do {
		/* Seek for frame start */
		if (pos + sizeof (header) > size) {
			return;
		}

		memcpy (&header, &data[pos], sizeof (header));

		if ((header & sync_mask) == sync_mask) {
			/* Found header sync */
			if (mp3_parse_header (data, size, pos, uri, metadata, filedata)) {
				return;
			}
		}

		pos++;
		counter++;
	} while (counter < MAX_MP3_SCAN_DEEP);
}

static void
get_id3v24_tags (const gchar *data,
		 size_t       size,
		 const gchar *uri,
		 GPtrArray  *metadata,
		 file_data   *filedata)
{
	guint pos = 0;
	Matches tmap[] = {
		{"TCOP", NIE_PREFIX "copyright", NULL, NULL, NULL},
		{"TDRC", NIE_PREFIX "contentCreated", NULL, NULL, NULL},
		{"TCON", NFO_PREFIX "genre", NULL, NULL, NULL},
		{"TIT1", NFO_PREFIX "genre", NULL, NULL, NULL},
		{"TENC", NCO_PREFIX "publisher", "publisher", NMM_PREFIX "Artist", NMM_PREFIX "artistName"},
		{"TEXT", NIE_PREFIX "plainTextContent", FALSE},
		{"TPE1", NMM_PREFIX "performer", "artist", NMM_PREFIX "Artist", NMM_PREFIX "artistName"},
		{"TPE2", NMM_PREFIX "performer", "artist", NMM_PREFIX "Artist", NMM_PREFIX "artistName"},
		{"TPE3", NMM_PREFIX "performer", "artist", NMM_PREFIX "Artist", NMM_PREFIX "artistName"},
		/*	{"TOPE", NID3_LEAD_ARTIST}, We dont' want the original artist for now */
		{"TPUB", NCO_PREFIX "publisher", "publisher", NMM_PREFIX "Artist", NMM_PREFIX "artistName"},
		{"TOAL", NIE_PREFIX "title", NULL, NULL, NULL},
		{"TALB", NIE_PREFIX "title" , NULL, NULL, NULL},
		{"TLAN", NIE_PREFIX "language", NULL, NULL, NULL},
		{"TIT2", NIE_PREFIX "title", NULL, NULL, NULL},
		{"TIT3", NIE_PREFIX "comment", NULL, NULL, NULL},
		{"TDRL", NIE_PREFIX "contentCreated", NULL, NULL, NULL},
		{"TRCK", NMM_PREFIX "trackNumber", NULL, NULL, NULL},
		/* TODO Nepomukify {"PCNT", "Audio:PlayCount"}, */
		{"TLEN", NMM_PREFIX "length", NULL, NULL, NULL},
		{NULL, 0, NULL, NULL, NULL},
	};

	while (pos < size) {
		size_t csize;
		gint i;
		unsigned short flags;

		if (pos + 10 > size) {
			return;
		}

		csize = (((data[pos+4] & 0x7F) << 21) |
			 ((data[pos+5] & 0x7F) << 14) |
			 ((data[pos+6] & 0x7F) << 7) |
			 ((data[pos+7] & 0x7F) << 0));

		if ((pos + 10 + csize > size) ||
		    (csize > size) ||
		    (csize == 0)) {
			break;
		}

		flags = (((unsigned char) (data[pos + 8]) << 8) + 
			 ((unsigned char) (data[pos + 9])));
		if (((flags & 0x80) > 0) ||
		    ((flags & 0x40) > 0)) {
			pos += 10 + csize;
			continue;
		}

		i = 0;
		while (tmap[i].text != NULL) {
			if (strncmp (tmap[i].text, (const char*) &data[pos], 4) == 0) {
				gchar * word;

				if ((flags & 0x20) > 0) {
					/* The "group" identifier, skip a byte */
					pos++;
					csize--;
				}

				/* This byte describes the encoding
				 * try to convert strings to UTF-8
				 * if it fails, then forget it
				 */

				switch (data[pos + 10]) {
				case 0x00:
					word = g_convert (&data[pos+11],
							  csize-1,
							  "UTF-8",
							  "ISO-8859-1",
							  NULL, NULL, NULL);
					break;
				case 0x01 :
					word = g_convert (&data[pos+11],
							  csize-1,
							  "UTF-8",
							  "UTF-16",
							  NULL, NULL, NULL);
					break;
				case 0x02 :
					word = g_convert (&data[pos+11],
							  csize-1,
							  "UTF-8",
							  "UTF-16BE",
							  NULL, NULL, NULL);
					break;
				case 0x03 :
					word = strndup (&data[pos+11], csize-1);
					break;

				default:
					/* Bad encoding byte,
					 * try to convert from
					 * iso-8859-1
					 */
					word = g_convert (&data[pos+11],
							  csize-1,
							  "UTF-8",
							  "ISO-8859-1",
							  NULL, NULL, NULL);
					break;
				}

				pos++;
				csize--;

				if (!tracker_is_empty_string (word)) {       
					if (strcmp (tmap[i].text, "TRCK") == 0) {
						gchar **parts;

						parts = g_strsplit (word, "/", 2);
						g_free (word);
						word = g_strdup (parts[0]);
						g_strfreev (parts);
					} else if (strcmp (tmap[i].text, "TCON") == 0) {
						gint genre;

						if (get_genre_number (word, &genre)) {
							g_free (word);
							word = g_strdup (get_genre_name (genre));
						}

						if (!word || strcasecmp (word, "unknown") == 0) {
							break;
						}
					} else if (strcmp (tmap[i].text, "TLEN") == 0) {
						guint32 duration;

						duration = atoi (word);
						g_free (word);
						word = g_strdup_printf ("%d", duration/1000);
						filedata->duration = duration/1000;
					}

					if (tmap[i].urn) {
						gchar *canonical_uri = tmap[i].urn[0]!=':'?tracker_uri_printf_escaped ("urn:%s:%s", tmap[i].urn, word):g_strdup(tmap[i].urn);
						tracker_statement_list_insert (metadata, canonical_uri, RDF_TYPE, tmap[i].rdf_type);
						tracker_statement_list_insert (metadata, canonical_uri, tmap[i].predicate, word);
						tracker_statement_list_insert (metadata, uri, tmap[i].type, canonical_uri);
						g_free (canonical_uri);
					} else {
						tracker_statement_list_insert (metadata, uri,
									  tmap[i].type,
									  word);
					}
				}

				g_free (word);

				break;
			}

			i++;
		}

		if (strncmp (&data[pos], "COMM", 4) == 0) {
			gchar       *word;
			gchar        text_encode;
			const gchar *text_language;
			const gchar *text_desc;
			const gchar *text;
			guint        offset;
			gint         text_desc_len;

			text_encode   =  data[pos + 10]; /* $xx */
			text_language = &data[pos + 11]; /* $xx xx xx */
			text_desc     = &data[pos + 14]; /* <text string according to encoding> $00 (00) */
			text_desc_len = strlen (text_desc);
			text          = &data[pos + 14 + text_desc_len + 1]; /* <full text string according to encoding> */
			
			offset = 4 + text_desc_len + 1;

			switch (text_encode) {
			case 0x00:
				word = g_convert (text,
						  csize - offset,
						  "UTF-8",
						  "ISO-8859-1",
						  NULL, NULL, NULL);
				break;
			case 0x01 :
				word = g_convert (text,
						  csize - offset,
						  "UTF-8",
						  "UTF-16",
						  NULL, NULL, NULL);
				break;
			case 0x02 :
				word = g_convert (text,
						  csize-offset,
						  "UTF-8",
						  "UTF-16BE",
						  NULL, NULL, NULL);
				break;
			case 0x03 :
				word = g_strndup (text, csize - offset);
				break;
				
			default:
				/* Bad encoding byte,
				 * try to convert from
				 * iso-8859-1
				 */
				word = g_convert (text,
						  csize - offset,
						  "UTF-8",
						  "ISO-8859-1",
						  NULL, NULL, NULL);
				break;
			}

			if (!tracker_is_empty_string (word)) {
				tracker_statement_list_insert (metadata, uri,
						     NIE_PREFIX "comment",
						     word);
			}

			g_free (word);
		}


		/* Check for embedded images */
		if (strncmp (&data[pos], "APIC", 4) == 0) {
			gchar        text_type;
			const gchar *mime;
			gchar        pic_type;
			const gchar *desc;
			guint        offset;
			gint         mime_len;

			text_type =  data[pos + 10];
			mime      = &data[pos + 11];
			mime_len  = strlen (mime);
			pic_type  =  data[pos + 11 + mime_len + 1];
			desc      = &data[pos + 11 + mime_len + 1 + 1];

			if (pic_type == 3 || (pic_type == 0 && filedata->albumartsize == 0)) {
				offset = pos + 11 + mime_len + 2 + strlen (desc) + 1;

				filedata->albumartdata = g_malloc0 (csize);
				memcpy (filedata->albumartdata, &data[offset], csize);
				filedata->albumartsize = csize;
			}
		}

		pos += 10 + csize;
	}
}

static void
get_id3v23_tags (const gchar *data,
		 size_t       size,
		 const gchar *uri,
		 GPtrArray  *metadata,
		 file_data   *filedata)
{
	guint	pos = 0;
	Matches tmap[] = {
		{"TCOP", NIE_PREFIX "copyright", NULL, NULL, NULL},
		{"TDAT", NIE_PREFIX "contentCreated", NULL, NULL, NULL},
		{"TCON", NFO_PREFIX "genre", NULL, NULL, NULL},
		{"TIT1", NFO_PREFIX "genre", NULL, NULL, NULL},
		{"TENC", NCO_PREFIX "publisher", "publisher", NMM_PREFIX "Artist", NMM_PREFIX "artistName"},
		{"TEXT", NIE_PREFIX "plainTextContent", NULL, NULL, NULL},
		{"TPE1", NMM_PREFIX "performer", "artist", NMM_PREFIX "Artist", NMM_PREFIX "artistName"},
		{"TPE2", NMM_PREFIX "performer", "artist", NMM_PREFIX "Artist", NMM_PREFIX "artistName"},
		{"TPE3", NMM_PREFIX "performer", "artist", NMM_PREFIX "Artist", NMM_PREFIX "artistName"},
		/*	{"TOPE", NID3_LEAD_ARTIST}, We don't want the original artist for now */
		{"TPUB", NCO_PREFIX "publisher", "publisher", NMM_PREFIX "Artist", NMM_PREFIX "artistName"},
		{"TOAL", NIE_PREFIX "title", NULL, NULL, NULL},
		{"TALB", NIE_PREFIX "title", NULL, NULL, NULL},
		{"TLAN", NIE_PREFIX "language", NULL, NULL, NULL},
		{"TIT2", NIE_PREFIX "title", NULL, NULL, NULL},
		{"TYER", NIE_PREFIX "contentCreated", NULL, NULL, NULL},
		{"TRCK", NMM_PREFIX "trackNumber", NULL, NULL, NULL},
		/* TODO Nepomukify {"PCNT", "Audio:PlayCount"}, */
		{"TLEN", NMM_PREFIX "duration", NULL, NULL, NULL},
		{NULL, 0, NULL, NULL, NULL},
	};

	while (pos < size) {
		size_t csize;
		gint i;
		unsigned short flags;

		if (pos + 10 > size) {
			return;
		}

		csize = (((unsigned char)(data[pos + 4]) << 24) |
			 ((unsigned char)(data[pos + 5]) << 16) |
			 ((unsigned char)(data[pos + 6]) << 8)  |
			 ((unsigned char)(data[pos + 7]) << 0) );

		if ((pos + 10 + csize > size) ||
		    (csize > size) ||
		    (csize == 0)) {
			break;
		}

		flags = (((unsigned char)(data[pos + 8]) << 8) + 
			 ((unsigned char)(data[pos + 9])));

		if (((flags & 0x80) > 0) || ((flags & 0x40) > 0)) {
			pos += 10 + csize;
			continue;
		}

		i = 0;
		while (tmap[i].text != NULL) {
			if (strncmp (tmap[i].text, (const gchar*) &data[pos], 4) == 0) {
				gchar * word;

				if ((flags & 0x20) > 0) {
					/* The "group" identifier, skip a byte */
					pos++;
					csize--;
				}

				/* This byte describes the encoding
				 * try to convert strings to UTF-8 if
				 * it fails, then forget it./
				 */

				switch (data[pos + 10]) {
				case 0x00:
					word = g_convert (&data[pos+11],
							  csize-1,
							  "UTF-8",
							  "ISO-8859-1",
							  NULL, NULL, NULL);
					break;
				case 0x01 :
/* 					word = g_convert (&data[pos+11], */
/* 							  csize-1, */
/* 							  "UTF-8", */
/* 							  "UCS-2", */
/* 							  NULL, NULL, NULL); */
					word = ucs2_to_utf8 (&data[pos+11],
							     csize-1);
					break;
				default:
					/* Bad encoding byte,
					 * try to convert from
					 * iso-8859-1
					 */
					word = g_convert (&data[pos+11],
							  csize-1,
							  "UTF-8",
							  "ISO-8859-1",
							  NULL, NULL, NULL);
					break;
				}

				pos++;
				csize--;

				if (!tracker_is_empty_string (word)) {
					if (strcmp (tmap[i].text, "TRCK") == 0) {
						gchar **parts;

						parts = g_strsplit (word, "/", 2);
						g_free (word);
						word = g_strdup (parts[0]);
						g_strfreev (parts);
					} else if (strcmp (tmap[i].text, "TCON") == 0) {
						gint genre;

						if (get_genre_number (word, &genre)) {
							g_free (word);
							word = g_strdup (get_genre_name (genre));
						}

						if (!word || strcasecmp (word, "unknown") == 0) {
							break;
						}
					} else if (strcmp (tmap[i].text, "TLEN") == 0) {
						guint32 duration;

						duration = atoi (word);
						g_free (word);
						word =  g_strdup_printf ("%d", duration/1000);
						filedata->duration = duration/1000;
					}

					if (tmap[i].urn) {
						gchar *canonical_uri = tmap[i].urn[0]!=':'?tracker_uri_printf_escaped ("urn:%s:%s", tmap[i].urn, word):g_strdup(tmap[i].urn);
						tracker_statement_list_insert (metadata, canonical_uri, RDF_TYPE, tmap[i].rdf_type);
						tracker_statement_list_insert (metadata, canonical_uri, tmap[i].predicate, word);
						tracker_statement_list_insert (metadata, uri, tmap[i].type, canonical_uri);
						g_free (canonical_uri);
					} else {
						tracker_statement_list_insert (metadata, uri,
									  tmap[i].type,
									  word);
					}
				}

				g_free (word);

				break;
			}

			i++;
		}

		if (strncmp (&data[pos], "COMM", 4) == 0) {
			gchar       *word;
			gchar        text_encode;
			const gchar *text_language;
			const gchar *text_desc;
			const gchar *text;
			guint        offset;
			gint         text_desc_len;
			
			text_encode   =  data[pos + 10]; /* $xx */
			text_language = &data[pos + 11]; /* $xx xx xx */
			text_desc     = &data[pos + 14]; /* <text string according to encoding> $00 (00) */
			text_desc_len = strlen (text_desc);
			text          = &data[pos + 14 + text_desc_len + 1]; /* <full text string according to encoding> */
			
			offset = 4 + text_desc_len + 1;

			switch (text_encode) {
			case 0x00:
				word = g_convert (text,
						  csize - offset,
						  "UTF-8",
						  "ISO-8859-1",
						  NULL, NULL, NULL);
				break;
			case 0x01 :
/* 				word = g_convert (text, */
/* 						  csize-offset, */
/* 						  "UTF-8", */
/* 						  "UCS-2", */
/* 						  NULL, NULL, NULL); */
				word = ucs2_to_utf8 (&data[pos + 11],
						     csize - offset);
				break;
			default:
				/* Bad encoding byte,
				 * try to convert from
				 * iso-8859-1
				 */
				word = g_convert (text,
						  csize - offset,
						  "UTF-8",
						  "ISO-8859-1",
						  NULL, NULL, NULL);
				break;
			}

			if (!tracker_is_empty_string (word)) {
				tracker_statement_list_insert (metadata, uri,
						     NIE_PREFIX "comment",
						     word);
			}

			g_free (word);
		}

		/* Check for embedded images */
		if (strncmp (&data[pos], "APIC", 4) == 0) {
			gchar        text_type;
			const gchar *mime;
			gchar        pic_type;
			const gchar *desc;
			guint        offset;
			gint         mime_len;

			text_type =  data[pos +10];
			mime      = &data[pos +11];
			mime_len  = strlen (mime);
			pic_type  =  data[pos +11 + mime_len + 1];
			desc      = &data[pos +11 + mime_len + 1 + 1];
			
			if (pic_type == 3 || (pic_type == 0 && filedata->albumartsize == 0)) {
				offset = pos + 11 + mime_len + 2 + strlen (desc) + 1;
				
				filedata->albumartdata = g_malloc0 (csize);
				memcpy (filedata->albumartdata, &data[offset], csize);
				filedata->albumartsize = csize;
			}
		}

		pos += 10 + csize;
	}
}

static void
get_id3v20_tags (const gchar *data,
		size_t	     size,
		const gchar *uri,
		GPtrArray  *metadata,
		 file_data   *filedata)
{
	guint	pos = 0;
	Matches tmap[] = {
		{"TAL", NIE_PREFIX "musicAlbum", "album", NMM_PREFIX "MusicAlbum", NMM_PREFIX "albumTitle"},
		{"TT1", NMM_PREFIX "performer", "artist", NMM_PREFIX "Artist", NMM_PREFIX "artistName"},
		{"TT2", NIE_PREFIX "title", NULL, NULL, NULL},
		{"TT3", NIE_PREFIX "title", NULL, NULL, NULL},
		{"TXT", NIE_PREFIX "comment", NULL, NULL, NULL},
		{"TPB", NCO_PREFIX "publisher", "publisher", NMM_PREFIX "Artist", NMM_PREFIX "artistName"},
		/* TODO {"WAF", "DC:Location", NULL, NULL, NULL},
		   TODO {"WAR", "DC:Location", NULL, NULL, NULL},
		   TODO {"WAS", "DC:Location", NULL, NULL, NULL},
		   TODO {"WAF", "DC:Location", NULL, NULL, NULL}, */
		{"WCM", NIE_PREFIX "license", NULL, NULL, NULL},
		{"TYE", NIE_PREFIX "contentCreated"},
		{"TLA", NIE_PREFIX "language", NULL, NULL, NULL},
		{"TP1", NMM_PREFIX "performer", "artist", NMM_PREFIX "Artist", NMM_PREFIX "artistName"},
		{"TP2", NMM_PREFIX "performer", "artist", NMM_PREFIX "Artist", NMM_PREFIX "artistName"},
		{"TP3", NMM_PREFIX "performer", "artist", NMM_PREFIX "Artist", NMM_PREFIX "artistName"},
		{"TEN", NMM_PREFIX "performer", "artist", NMM_PREFIX "Artist", NMM_PREFIX "artistName"},
		{"TCO", NMM_PREFIX "genre", NULL, NULL, NULL},
		{"TCR", NIE_PREFIX "copyright", NULL, NULL, NULL},
		{"SLT", NIE_PREFIX "plainTextContent"}, /* Lyrics */
		{"TOA", NMM_PREFIX "performer", "artist", NMM_PREFIX "Artist", NMM_PREFIX "artistName"},
		{"TOT", NIE_PREFIX "title", NULL, NULL, NULL},
		{"TOL", NMM_PREFIX "performer", "artist", NMM_PREFIX "Artist", NMM_PREFIX "artistName"},
		{"COM", NIE_PREFIX "comment", NULL, NULL, NULL},
		{"TLE", NMM_PREFIX "duration", NULL, NULL, NULL},
		{ NULL, 0, NULL, NULL, NULL},
	};

	while (pos < size) {
		size_t csize;
		gint i;

		if (pos + 6 > size)  {
			return;
		}

		csize = (((unsigned char)(data[pos + 3]) << 16) + 
			 ((unsigned char)(data[pos + 4]) << 8) + 
			 ((unsigned char)(data[pos + 5]) ) );
		if ((pos + 6 + csize > size) ||
		    (csize > size) ||
		    (csize == 0)) {
			break;
		}

		i = 0;

		while (tmap[i].text != NULL) {
			if (strncmp(tmap[i].text, (const char*) &data[pos], 3) == 0) {
				gchar * word;

				/* This byte describes the encoding
				 * try to convert strings to UTF-8 if
				 * it fails, then forget it./
				 */
				switch (data[pos + 6]) {
				case 0x00:
					word = g_convert (&data[pos+7],
							  csize-1,
							  "UTF-8",
							  "ISO-8859-1",
							  NULL, NULL, NULL);
					break;
				case 0x01 :
/* 					word = g_convert (&data[pos+7], */
/* 							  csize, */
/* 							  "UTF-8", */
/* 							  "UCS-2", */
/* 							  NULL, NULL, NULL); */
					word = ucs2_to_utf8 (&data[pos+7],
							     csize-1);
					break;
				default:
					/* Bad encoding byte,
					 * try to convert from
					 * iso-8859-1
					 */
					word = g_convert (&data[pos+7],
							  csize-1,
							  "UTF-8",
							  "ISO-8859-1",
							  NULL, NULL, NULL);
					break;
				}

				pos++;
				csize--;

				if (!tracker_is_empty_string (word)) {
					if (strcmp (tmap[i].text, "COM") == 0) {
						gchar *s;

						s = g_strdup (word + strlen (word) + 1);
						g_free (word);
						word = s;
					}

					if (strcmp (tmap[i].text, "TCO") == 0) {
						gint genre;
						if (get_genre_number (word, &genre)) {
							g_free (word);
							word = g_strdup (get_genre_name (genre));
						}

						if (!word || strcasecmp (word, "unknown") == 0) {
							g_free (word);
							break;
						}
					} else if (strcmp (tmap[i].text, "TLE") == 0) {
						guint32 duration;

						duration = atoi (word);
						g_free (word);
						word = g_strdup_printf ("%d", duration/1000);
						filedata->duration = duration/1000;
					}

					if (tmap[i].urn) {
						gchar *canonical_uri = tmap[i].urn[0]!=':'?tracker_uri_printf_escaped ("urn:%s:%s", tmap[i].urn, word):g_strdup(tmap[i].urn);
						tracker_statement_list_insert (metadata, canonical_uri, RDF_TYPE, tmap[i].rdf_type);
						tracker_statement_list_insert (metadata, canonical_uri, tmap[i].predicate, word);
						tracker_statement_list_insert (metadata, uri, tmap[i].type, canonical_uri);
						g_free (canonical_uri);
					} else {
						tracker_statement_list_insert (metadata, uri,
									  tmap[i].type,
									  word);
					}
				} else {
					g_free (word);
				}

				break;
			}

			i++;
		}

		/* Check for embedded images */
		if (strncmp (&data[pos], "PIC", 3) == 0) {
			gchar          pic_type;
			const gchar   *desc;
			guint          offset;

			pic_type  =  data[pos + 6 + 3 + 1 + 3];
			desc      = &data[pos + 6 + 3 + 1 + 3 + 1];

			if (pic_type == 3 || (pic_type == 0 && filedata->albumartsize == 0)) {
				offset = pos + 6 + 3 + 1 + 3  + 1 + strlen (desc) + 1;

				filedata->albumartdata = g_malloc0 (csize);
				memcpy (filedata->albumartdata, &data[offset], csize);
				filedata->albumartsize = csize;
			}
		}

		pos += 6 + csize;
	}
}

static void
parse_id3v24 (const gchar *data,
	      size_t       size,
	      const gchar *uri,
	      GPtrArray  *metadata,
	      file_data   *filedata,
	      size_t      *offset_delta)
{
	gint	unsync;
	gint	extendedHdr;
	gint	experimental;
	gint	footer;
	guint	tsize;
	guint	pos;
	guint	ehdrSize;
	guint	padding;

	if ((size < 16) ||
	    (data[0] != 0x49) ||
	    (data[1] != 0x44) ||
	    (data[2] != 0x33) ||
	    (data[3] != 0x04) ||
	    (data[4] != 0x00) ) {
		return;
	}

	unsync = (data[5] & 0x80) > 0;
	extendedHdr = (data[5] & 0x40) > 0;
	experimental = (data[5] & 0x20) > 0;
	footer = (data[5] & 0x10) > 0;
	tsize = (((data[6] & 0x7F) << 21) |
		 ((data[7] & 0x7F) << 14) |
		 ((data[8] & 0x7F) << 7) |
		 ((data[9] & 0x7F) << 0));

	if ((tsize + 10 > size) || (experimental)) {
		return;
	}

	pos = 10;
	padding = 0;

	if (extendedHdr) {
		ehdrSize = (((data[10] & 0x7F) << 21) |
			    ((data[11] & 0x7F) << 14) |
			    ((data[12] & 0x7F) << 7) |
			    ((data[13] & 0x7F) << 0));
		pos += ehdrSize;
	}

	if (unsync) {
		size_t  unsync_size;
		gchar  *body;

		un_unsync (&data[pos], tsize, (unsigned char **)&body, &unsync_size);
		get_id3v24_tags (body, unsync_size, uri, metadata, filedata);
		g_free (body);
	} else {
		get_id3v24_tags (&data[pos], tsize, uri, metadata, filedata);
	}

	*offset_delta = tsize + 10;
}

static void
parse_id3v23 (const gchar *data,
	      size_t       size,
	      const gchar *uri,
	      GPtrArray  *metadata,
	      file_data   *filedata,
	      size_t      *offset_delta)
{
	gint	unsync;
	gint	extendedHdr;
	gint	experimental;
	guint	tsize;
	guint	pos;
	guint	ehdrSize;
	guint	padding;

	if ((size < 16) ||
	    (data[0] != 0x49) ||
	    (data[1] != 0x44) ||
	    (data[2] != 0x33) ||
	    (data[3] != 0x03) ||
	    (data[4] != 0x00)) {
		return;
	}

	unsync = (data[5] & 0x80) > 0;
	extendedHdr = (data[5] & 0x40) > 0;
	experimental = (data[5] & 0x20) > 0;
	tsize = (((data[6] & 0x7F) << 21) |
		 ((data[7] & 0x7F) << 14) |
		 ((data[8] & 0x7F) << 7) |
		 ((data[9] & 0x7F) << 0));

	if ((tsize + 10 > size) || (experimental)) {
		return;
	}

	pos = 10;
	padding = 0;

	if (extendedHdr) {
		ehdrSize = (((unsigned char)(data[10]) << 24) |
			    ((unsigned char)(data[11]) << 16) |
			    ((unsigned char)(data[12]) << 8) |
			    ((unsigned char)(data[12]) << 0));

		padding	= (((unsigned char)(data[15]) << 24) |
			   ((unsigned char)(data[16]) << 16) |
			   ((unsigned char)(data[17]) << 8) |
			   ((unsigned char)(data[18]) << 0));

		pos += 4 + ehdrSize;

		if (padding < tsize)
			tsize -= padding;
		else {
			return;
		}
	}

	if (unsync) {
		size_t  unsync_size;
		gchar  *body;

		un_unsync (&data[pos], tsize, (unsigned char **)&body, &unsync_size);
		get_id3v23_tags (body, unsync_size, uri, metadata, filedata);
		g_free (body);
	} else {
		get_id3v23_tags (&data[pos], tsize, uri, metadata, filedata);
	}

	*offset_delta = tsize + 10;
}

static void
parse_id3v20 (const gchar *data,
	      size_t	      size,
	      const gchar *uri,
	      GPtrArray  *metadata,
	      file_data   *filedata,
	      size_t      *offset_delta)
{
	gint	unsync;
	guint	tsize;
	guint	pos;

	if ((size < 16) ||
	    (data[0] != 0x49) ||
	    (data[1] != 0x44) ||
	    (data[2] != 0x33) ||
	    (data[3] != 0x02) ||
	    (data[4] != 0x00)) {
		return;
	}

	unsync = (data[5] & 0x80) > 0;
	tsize = (((data[6] & 0x7F) << 21) |
		 ((data[7] & 0x7F) << 14) |
		 ((data[8] & 0x7F) << 07) |
		 ((data[9] & 0x7F) << 00));

	if (tsize + 10 > size)	{
		return;
	}
	pos = 10;

	if (unsync) {
		size_t  unsync_size;
		gchar  *body;

		un_unsync (&data[pos], tsize, (unsigned char **)&body, &unsync_size);
		get_id3v20_tags (body, unsync_size, uri, metadata, filedata);
		g_free (body);
	} else {
		get_id3v20_tags (&data[pos], tsize, uri, metadata, filedata);
	}

	*offset_delta = tsize + 10;
}

static goffset
parse_id3v2 (const gchar *data,
	     size_t	     size,
	     const gchar *uri,
	     GPtrArray  *metadata,
	     file_data   *filedata)
{
	gboolean done = FALSE;
	size_t   offset = 0;

	do {
		size_t offset_delta = 0;
		parse_id3v24 (data+offset, size-offset, uri, metadata, filedata, &offset_delta);
		parse_id3v23 (data+offset, size-offset, uri, metadata, filedata, &offset_delta);
		parse_id3v20 (data+offset, size-offset, uri, metadata, filedata, &offset_delta);		

		if (offset_delta == 0) {
			done = TRUE;
			filedata->id3v2_size = offset;
		} else {
			offset += offset_delta;
		}

	} while (!done);

	return offset;
}

static void
extract_mp3 (const gchar *uri,
	     GPtrArray  *metadata)
{
	gchar       *filename;
	int	     fd;
	void	    *buffer;
	void        *id3v1_buffer;
	goffset      size;
	id3tag	     info;
	goffset      audio_offset;
	file_data    filedata;

	info.title = NULL;
	info.artist = NULL;
	info.album = NULL;
	info.year = NULL;
	info.comment = NULL;
	info.genre = NULL;
	info.trackno = NULL;

	filedata.size = 0;
	filedata.id3v2_size = 0;
	filedata.duration = 0;
	filedata.albumartdata = NULL;
	filedata.albumartsize = 0;

	filename = g_filename_from_uri (uri, NULL, NULL);

	size = tracker_file_get_size (filename);

	if (size == 0) {
		g_free (filename);
		return;
	}

	filedata.size = size;

#if defined(__linux__)
	/* Can return -1 because of O_NOATIME, so we try again after
	 * without as a last resort. This can happen due to
	 * permissions.
	 */
	fd = open (filename, O_RDONLY | O_NOATIME);
	if (fd == -1) {
		fd = open (filename, O_RDONLY);
		
		if (fd == -1) {
			return;
		}
	}
#else
	fd = open (filename, O_RDONLY);
	if (fd == -1) {
		return;
	}
#endif

#ifndef G_OS_WIN32
	/* We don't use GLib's mmap because size can not be specified */
	buffer = mmap (NULL, 
		       MIN (size, MAX_FILE_READ), 
		       PROT_READ, 
		       MAP_PRIVATE, 
		       fd, 
		       0);
#endif

	id3v1_buffer = read_id3v1_buffer (fd, size);

	close (fd);

	if (buffer == NULL || buffer == (void*) -1) {
		g_free (filename);
		return;
	}

	if (!get_id3 (id3v1_buffer, ID3V1_SIZE, &info)) {
		/* Do nothing? */
	}

	tracker_statement_list_insert (metadata, uri, 
	                          RDF_TYPE, 
	                          NMM_PREFIX "MusicPiece");

	if (!tracker_is_empty_string (info.title)) {
		tracker_statement_list_insert (metadata, uri,
				     NIE_PREFIX "title",
				     info.title);
	}

	if (!tracker_is_empty_string (info.artist)) {
		gchar *canonical_uri = tracker_uri_printf_escaped ("urn:artist:%s", info.artist);
		tracker_statement_list_insert (metadata, canonical_uri, RDF_TYPE, NMM_PREFIX "Artist");
		tracker_statement_list_insert (metadata, canonical_uri, NMM_PREFIX "artistName", info.artist);
		tracker_statement_list_insert (metadata, uri, NMM_PREFIX "performer", canonical_uri);
		g_free (canonical_uri);
	}

	if (!tracker_is_empty_string (info.album)) {
		gchar *canonical_uri = tracker_uri_printf_escaped ("urn:album:%s", info.album);
		tracker_statement_list_insert (metadata, canonical_uri, RDF_TYPE, NMM_PREFIX "MusicAlbum");
		tracker_statement_list_insert (metadata, canonical_uri, NMM_PREFIX "albumTitle", info.album);
		tracker_statement_list_insert (metadata, uri, NMM_PREFIX "musicAlbum", canonical_uri);
		g_free (canonical_uri);
	}

	if (!tracker_is_empty_string (info.year)) {
		tracker_statement_list_insert (metadata, uri,
				     NIE_PREFIX "contentCreated",
				     info.year);
	}

	if (!tracker_is_empty_string (info.genre)) {
		tracker_statement_list_insert (metadata, uri,
				     NFO_PREFIX "genre",
				     info.genre);
	}

	if (!tracker_is_empty_string (info.comment)) {
		tracker_statement_list_insert (metadata, uri,
				     NIE_PREFIX "comment",
				     info.comment);
	}

	if (!tracker_is_empty_string (info.trackno)) {
		tracker_statement_list_insert (metadata, uri,
				     NMM_PREFIX "trackNumber",
				     info.trackno);
	}

	g_free (info.title);
	g_free (info.year);
	g_free (info.album);
	g_free (info.artist);
	g_free (info.comment);
	g_free (info.trackno);
	g_free (info.genre);

	/* Get other embedded tags */
	audio_offset = parse_id3v2 (buffer, MIN(size, MAX_FILE_READ), uri, metadata, &filedata);

	/* Get mp3 stream info */
	mp3_parse (buffer, MIN(size, MAX_FILE_READ), audio_offset, uri, metadata, &filedata);

	/* TODO */
#ifdef HAVE_GDKPIXBUF
	tracker_process_albumart (filedata.albumartdata, filedata.albumartsize,
				  /* tracker_statement_list_find (metadata, NMM_PREFIX "performer") */ NULL,
				  tracker_statement_list_find (metadata, uri, NIE_PREFIX "title"), "-1",
				  filename);
#else
	tracker_process_albumart (NULL, 0,
				  /* tracker_statement_list_find (metadata, NMM_PREFIX "performer") */ NULL,
				  tracker_statement_list_find (metadata, uri, NIE_PREFIX "title"), "-1",
				  filename);

#endif /* HAVE_GDKPIXBUF */

	if (filedata.albumartdata) {
		g_free (filedata.albumartdata);
	}

#ifndef G_OS_WIN32
	munmap (buffer, MIN(size, MAX_FILE_READ));
#endif

	g_free (id3v1_buffer);
	g_free (filename);
}

TrackerExtractData *
tracker_get_extract_data (void)
{
	return extract_data;
}
