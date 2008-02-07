/* 

	ts2mpa.c
	(C) Nicholas J Humfrey <njh@aelius.com> 2008
	
	Copyright notice:
	
	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.
	
	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.
	
	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the Free Software
	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
    
*/


#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>

#include "ts2mpa.h"
#include "mpa_header.h"

int Quiet = 0;
int Interrupted = 0;



// Check to see if a PES header looks like a valid MPEG Audio one
static int validate_pes_header( int pid, unsigned char* buf_ptr, int buf_len )
{
	unsigned char stream_id = 0x00;

	// Does it have the right magic?
	if( PES_PACKET_SYNC_BYTE1(buf_ptr) != 0x00 ||
	    PES_PACKET_SYNC_BYTE2(buf_ptr) != 0x00 ||
	    PES_PACKET_SYNC_BYTE3(buf_ptr) != 0x01 )
	{
		if (!Quiet) fprintf(stderr, "ts2mpa: Invalid PES header (pid: %d).\n", pid);
		return 0;
	}
	
	// Is it MPEG Audio?
	stream_id = PES_PACKET_STREAM_ID(buf_ptr);
	if (stream_id < 0xC0 || stream_id > 0xDF) {
		if (!Quiet) fprintf(stderr, "ts2mpa: Ignoring non-mpegaudio stream (pid: %d, stream id: 0x%x).\n", pid, stream_id);
		return 0;
	}

	// Check PES Extension header 
	if( PES_PACKET_SYNC_CODE(buf_ptr) != 0x2 )
	{
		if (!Quiet) fprintf(stderr, "ts2mpa: Invalid sync code PES extension header (pid: %d, stream id: 0x%x).\n", pid, stream_id);
		return 0;
	}

	// Reject scrambled packets
	if( PES_PACKET_SCRAMBLED(buf_ptr) )
	{
		if (!Quiet) fprintf(stderr, "ts2mpa: PES payload is scrambled (pid: %d, stream id: 0x%x).\n", pid, stream_id);
		return 0;
	}

	// It is valid
	return 1;
}


// Extract the PES payload and send it to the output file
static void extract_pes_payload( ts2mpa_t *ts2mpa, unsigned char *pes_ptr, size_t pes_len, int start_of_pes ) 
{
	unsigned char* es_ptr=NULL;
	size_t es_len=0;
	
	
	// Start of a PES header?
	if ( start_of_pes ) {
		unsigned int pes_total_len = PES_PACKET_LEN(pes_ptr);
		size_t pes_header_len = PES_PACKET_HEAD_LEN(pes_ptr);
		unsigned char stream_id = PES_PACKET_STREAM_ID(pes_ptr);
	
		// Check that it has a valid header
		if (!validate_pes_header( ts2mpa->pid, pes_ptr, pes_len )) return;
		
		// Stream IDs in range 0xC0-0xDF are MPEG audio
		if( stream_id != ts2mpa->pes_stream_id )
		{	
			if (ts2mpa->pes_stream_id == -1) {
				// keep the first stream we see
				ts2mpa->pes_stream_id = stream_id;	
				if (!Quiet)
					fprintf(stderr, "ts2mpa: Found valid PES audio packet (offset: 0x%lx, pid: %d, stream id: 0x%x, length: %u)\n",
									((unsigned long)ts2mpa->total_packets-1)*TS_PACKET_SIZE, ts2mpa->pid, stream_id, pes_total_len);
			} else {
				if (!Quiet)
					fprintf(stderr, "ts2mpa: Ignoring additional audio stream ID 0x%x (pid: %d).\n",
							stream_id, ts2mpa->pid);
				return;
			}
		}
	
		// Store the length of the PES packet payload
		ts2mpa->pes_remaining = pes_total_len - (2+pes_header_len);
	
		// Keep pointer to ES data in this packet
		es_ptr = pes_ptr+(9+pes_header_len);
		es_len = pes_len-(9+pes_header_len);
	

	} else if (ts2mpa->pes_stream_id) {
	
		// Only output data once we have seen a PES header
		es_ptr = pes_ptr;
		es_len = pes_len;
	
		// Are we are the end of the PES packet?
		if (es_len>ts2mpa->pes_remaining) {
			es_len=ts2mpa->pes_remaining;
		}
		
	}

	
	// Got some data to write out?
	if (es_ptr) {
		
		// Subtract the amount remaining in current PES packet
		ts2mpa->pes_remaining -= es_len;
	
		// Scan through Elementary Stream (ES) 
		// and try and find MPEG audio stream header
		while (!ts2mpa->synced && es_len>=4) {
		
			// Valid header?
			if (mpa_header_parse(es_ptr, &ts2mpa->mpah)) {

				// Looks good, we have gained sync.
				if (!Quiet) {
				  if (ts2mpa->never_synced) {
            fprintf(stderr, "ts2mpa: ");
            mpa_header_print( &ts2mpa->mpah );
            fprintf(stderr, "ts2mpa: MPEG Audio Framesize: %d bytes\n", ts2mpa->mpah.framesize);
          } else {
            fprintf(stderr, "ts2mpa: Regained sync at 0x%lx\n", ((unsigned long)ts2mpa->total_packets-1)*TS_PACKET_SIZE);
          }
				}
				ts2mpa->synced = 1;
				ts2mpa->never_synced = 0;

			} else {
				// Skip byte
				es_len--;
				es_ptr++;
			}
		}
		
		
		// If stream is synced then write the data out
		if (ts2mpa->synced && es_len > 0) {
			int written = 0;
			
			// Write out the data
			written = fwrite( es_ptr, es_len, 1, ts2mpa->output );
			if (written<=0) {
				perror("Error: failed to write stream out");
				exit(-2);
			}
			ts2mpa->total_bytes += written;
		}
	}

}



static void ts_continuity_check( ts2mpa_t *ts2mpa, int ts_cc ) 
{
	if (ts2mpa->continuity_count != ts_cc) {
	
		// Only display an error after we gain sync
		if (ts2mpa->synced) {
			if (!Quiet) {
			  fprintf(stderr, "ts2mpa: Warning, TS continuity error at 0x%lx\n",
			    ((unsigned long)ts2mpa->total_packets-1)*TS_PACKET_SIZE);
			}
			ts2mpa->synced=0;
		}
		ts2mpa->continuity_count = ts_cc;
	}

	ts2mpa->continuity_count++;
	if (ts2mpa->continuity_count==16)
		ts2mpa->continuity_count=0;
}



static void process_ts_packets( ts2mpa_t *ts2mpa )
{
	unsigned char buf[TS_PACKET_SIZE];
	unsigned char* pes_ptr=NULL;
	size_t pes_len;
	size_t count;

	while ( !Interrupted ) {
		
		count = fread(buf, TS_PACKET_SIZE, 1, ts2mpa->input);
		if (count==0) break;
		ts2mpa->total_packets++;
		
		// Check the sync-byte
		if (TS_PACKET_SYNC_BYTE(buf) != 0x47) {
			fprintf(stderr,"ts2mpa: Lost Transport Stream syncronisation - aborting (offset: 0x%lx).\n",
			  ((unsigned long)ts2mpa->total_packets-1)*TS_PACKET_SIZE);
			// FIXME: try and re-gain synchronisation
			break;
		}

		// Check packet validity
		if (TS_PACKET_PID(buf) == ts2mpa->pid || ts2mpa->pid == -1) {
      // Scrambled?
      if ( TS_PACKET_SCRAMBLING(buf) ) {
        if (!Quiet) fprintf(stderr, "ts2mpa: Warning, PID %d is scrambled.\n", TS_PACKET_PID(buf));
        continue;
      }	
		
      // Transport error?
		  if ( TS_PACKET_TRANS_ERROR(buf) ) {
        if (!Quiet) fprintf(stderr, "ts2mpa: Warning, transport error at 0x%lx\n", ((unsigned long)ts2mpa->total_packets-1)*TS_PACKET_SIZE);
        ts2mpa->synced = 0;
		    continue;
      }
 		}

		// Location of and size of PES payload
		pes_ptr = &buf[4];
		pes_len = TS_PACKET_SIZE - 4;

		// Check for adaptation field?
		if (TS_PACKET_ADAPTATION(buf)==0x1) {
			// Payload only, no adaptation field
		} else if (TS_PACKET_ADAPTATION(buf)==0x2) {
			// Adaptation field only, no payload
			continue;
		} else if (TS_PACKET_ADAPTATION(buf)==0x3) {
			// Adaptation field AND payload
			pes_ptr += (TS_PACKET_ADAPT_LEN(buf) + 1);
			pes_len -= (TS_PACKET_ADAPT_LEN(buf) + 1);
		}

		// Check we know about the payload
		if (TS_PACKET_PID(buf) == 0x1FFF) {
			// Ignore NULL package
			continue;
		}
		
		// No chosen PID yet?
		if (ts2mpa->pid == -1 && TS_PACKET_PAYLOAD_START(buf)) {

			// Does this one look good ?
			if (TS_PACKET_PAYLOAD_START(buf) && 
			    validate_pes_header( TS_PACKET_PID(buf), pes_ptr, pes_len ))
			{
				// Looks good, use this one
				ts2mpa->pid = TS_PACKET_PID(buf);
			}
		}

		// Process the packet, if it is the PID we are interested in		
		if (TS_PACKET_PID(buf) == ts2mpa->pid) {
		
			// Continuity check
			ts_continuity_check( ts2mpa, TS_PACKET_CONT_COUNT(buf) );
		
			// Extract PES payload and write it to output
			extract_pes_payload( ts2mpa, pes_ptr, pes_len, TS_PACKET_PAYLOAD_START(buf) );
		}

	}

	
}

static ts2mpa_t * init_ts2mpa_t()
{
	ts2mpa_t *ts2mpa = malloc( sizeof(ts2mpa_t) );
	if (ts2mpa==NULL) {
		perror("Failed to allocate memory for ts2mpa_t");
		exit(-3);
	}
	
	// Zero the memory
	bzero( ts2mpa, sizeof(ts2mpa_t) );
	
	// Initialise defaults
	ts2mpa->input = NULL;
	ts2mpa->output = NULL;
	ts2mpa->pid = -1;
	ts2mpa->synced = 0;
	ts2mpa->never_synced = 1;
	ts2mpa->continuity_count = -1;
	ts2mpa->pes_stream_id = -1;
	ts2mpa->pes_remaining = 0;
	ts2mpa->total_bytes = 0;
	ts2mpa->total_packets = 0;

	return ts2mpa;
}


static void usage()
{
	fprintf( stderr, "Usage: ts2mpa [options] <infile> <outfile>\n" );
	fprintf( stderr, "    -h             Help - this message.\n" );
	fprintf( stderr, "    -q             Quiet - don't print messages to stderr.\n" );
	fprintf( stderr, "    -p <pid>       Choose a specific transport stream PID.\n" );
	fprintf( stderr, "    -s <streamid>  Choose a specific PES stream ID.\n" );
	exit(-1);
}


static int parse_value( char* str )
{
	int value = 0;
	
	// Is it a hexadecimal?
	if (str[0] == '0' && (str[1] == 'x' || str[1] == 'X')) {
		str += 2;
		if (sscanf(str, "%x", &value)<=0) {
			fprintf(stderr, "ts2mpa: failed to parse hexadeicmal number.\n");
			usage();
		} else {
			return value;
		}
	} else {
		return atoi( str );
	}
	
	
	// Shouldn't get here
	return -1;
}


static void parse_cmd_line( ts2mpa_t *ts2mpa, int argc, char** argv )
{
	int ch;


	// Parse the options/switches
	while ((ch = getopt(argc, argv, "p:s:qh?")) != -1)
	switch (ch) {
		case 'q':
			Quiet = 1;
		break;
	
		case 'p':
			ts2mpa->pid = parse_value( optarg );
			if (ts2mpa->pid <= 0) {
				fprintf(stderr, "ts2mpa: Invalid Transport Stream PID: %s\n", optarg);
				exit(-1);
			}
		break;

		case 's':
			ts2mpa->pes_stream_id = parse_value( optarg );
			if (ts2mpa->pes_stream_id <= 0) {
				fprintf(stderr, "ts2mpa: Invalid PES Stream ID: %s\n", optarg);
				exit(-1);
			}
		break;

		case '?':
		case 'h':
		default:
			usage();
	}


	// Open the input file
	if (argc-optind < 1) {
		fprintf(stderr, "ts2mpa: missing input and output files.\n");
		usage();
	} else if ( strncmp( argv[optind], "-", 1 ) == 0 ) {
		// Use STDIN
		ts2mpa->input = stdin;
	} else {
		ts2mpa->input = fopen( argv[optind], "rb" );
		if (ts2mpa->input==NULL) {
			perror("ts2mpa: Failed to open input file");
			exit(-2);
		}
	}

	// Open the output file
	if (argc-optind < 2) {
		fprintf(stderr, "ts2mpa: missing output file.\n");
		usage();
	} else if ( strncmp( argv[optind+1], "-", 1 ) == 0 ) {
		// Use STDOUT
		ts2mpa->output = stdout;
	} else {
		ts2mpa->output = fopen( argv[optind+1], "wb" );
		if (ts2mpa->output==NULL) {
			perror("ts2mpa: Failed to open output file");
			exit(-2);
		}
	}
}

static void termination_handler(int signum)
{
	if (signum==SIGINT) fprintf(stderr, "ts2mpa: Recieved SIGINT, aborting.\n");
	else if (signum==SIGTERM) fprintf(stderr, "ts2mpa: Recieved SIGTERM, aborting.\n");
	else if (signum==SIGHUP) fprintf(stderr, "ts2mpa: Recieved SIGHUP, aborting.\n");
	
	Interrupted = 1;
}


int main( int argc, char** argv )
{
	ts2mpa_t *ts2mpa = init_ts2mpa_t();

	// Parse the command-line parameters
	parse_cmd_line( ts2mpa, argc, argv );
	
	// Setup signal handling - so we exit cleanly
	if (signal (SIGINT, termination_handler) == SIG_IGN)
		signal (SIGINT, SIG_IGN);
	if (signal (SIGHUP, termination_handler) == SIG_IGN)
		signal (SIGHUP, SIG_IGN);
	if (signal (SIGTERM, termination_handler) == SIG_IGN)
		signal (SIGTERM, SIG_IGN);

	// Hard work happens here
	process_ts_packets( ts2mpa );
	
	// Display statistics
	if (!Quiet) {
    fprintf(stderr, "ts2mpa: TS packets processed: %lu\n", ts2mpa->total_packets);
    fprintf(stderr, "ts2mpa: Total written: %lu bytes\n", ts2mpa->total_bytes);
	}
	
	// Close the input and output files
	fclose( ts2mpa->input );
	fclose( ts2mpa->output );
	
	free(ts2mpa);
	
	// Success
	return 0;
}

