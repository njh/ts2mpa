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

#include "ts2mpa.h"
#include "mpa_header.h"

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
		fprintf(stderr, "Invalid PES header (pid: %d).\n", pid);
		return 0;
	}
	
	// Is it MPEG Audio?
	stream_id = PES_PACKET_STREAM_ID(buf_ptr);
	if (stream_id < 0xC0 || stream_id > 0xDF) {
		fprintf(stderr, "Ignoring non-mpegaudio stream (pid: %d, stream id: 0x%x).\n", pid, stream_id);
		return 0;
	}

	// Check PES Extension header 
	if( PES_PACKET_SYNC_CODE(buf_ptr) != 0x2 )
	{
		fprintf(stderr, "Error: invalid sync code PES extension header (pid: %d, stream id: 0x%x).\n", pid, stream_id);
		return 0;
	}

	// Reject scrambled packets
	if( PES_PACKET_SCRAMBLED(buf_ptr) )
	{
		fprintf(stderr, "Error: PES payload is scrambled (pid: %d, stream id: 0x%x).\n", pid, stream_id);
		return 0;
	}

	// It is valid
	return 1;
}


// Extract the PES payload and send it to the output file
static void extract_pes_payload( mpeg_stream_t *stream, unsigned char *pes_ptr, size_t pes_len, int start_of_pes ) 
{
	unsigned char* es_ptr=NULL;
	size_t es_len=0;
	
	
	// Start of a PES header?
	if ( start_of_pes ) {
		unsigned int pes_total_len = PES_PACKET_LEN(pes_ptr);
		size_t pes_header_len = PES_PACKET_HEAD_LEN(pes_ptr);
		unsigned char stream_id = PES_PACKET_STREAM_ID(pes_ptr);
	
		// Check that it has a valid header
		if (!validate_pes_header( stream->pid, pes_ptr, pes_len )) return;
		
		// Stream IDs in range 0xC0-0xDF are MPEG audio
		if( stream_id != stream->pes_stream_id )
		{	
			if (stream->pes_stream_id == 0) {
				// keep the first stream we see
				stream->pes_stream_id = stream_id;	
				fprintf(stderr, "Found valid PES audio packet (pid: %d, stream id: 0x%x, length: %u)\n",
								stream->pid, stream_id, pes_total_len);
			} else {
				fprintf(stderr, "Ignoring additional audio stream ID 0x%x (pid: %d).\n",
						stream_id, stream->pid);
				return;
			}
		}
	
		// Store the length of the PES packet payload
		stream->pes_remaining = pes_total_len - (2+pes_header_len);
	
		// Keep pointer to ES data in this packet
		es_ptr = pes_ptr+(9+pes_header_len);
		es_len = pes_len-(9+pes_header_len);
	

	} else if (stream->pes_stream_id) {
	
		// Only output data once we have seen a PES header
		es_ptr = pes_ptr;
		es_len = pes_len;
	
		// Are we are the end of the PES packet?
		if (es_len>stream->pes_remaining) {
			es_len=stream->pes_remaining;
		}
		
	}

	
	// Got some data to write out?
	if (es_ptr) {
		
		// Subtract the amount remaining in current PES packet
		stream->pes_remaining -= es_len;
	
		// Scan through Elementary Stream (ES) 
		// and try and find MPEG audio stream header
		while (!stream->synced && es_len>=4) {
		
			// Valid header?
			if (mpa_header_parse(es_ptr, &stream->mpah)) {

				// Looks good, we have gained sync.
				mpa_header_print( &stream->mpah );
				fprintf(stderr, "MPEG Audio Framesize: %d bytes\n", stream->mpah.framesize);
				stream->synced = 1;

			} else {
				// Skip byte
				es_len--;
				es_ptr++;
			}
		}
		
		
		// If stream is synced then put data info buffer
		if (stream->synced) {
			int written = 0;
			
			// Write out the data
			written = fwrite( es_ptr, es_len, 1, stream->output );
			if (written<=0) {
				perror("Error: failed to write stream out");
				exit(-2);
			}
			stream->total_bytes += written;
		}
	}

}



static void ts_continuity_check( mpeg_stream_t *stream, int ts_cc ) 
{
	if (stream->continuity_count != ts_cc) {
	
		// Only display an error after we gain sync
		if (stream->synced) {
			fprintf(stderr, "Warning: TS continuity error (pid: %d)\n", stream->pid );
			stream->synced=0;
		}
		stream->continuity_count = ts_cc;
	}

	stream->continuity_count++;
	if (stream->continuity_count==16)
		stream->continuity_count=0;
}



static void process_ts_packets( mpeg_stream_t *stream )
{
	unsigned char buf[TS_PACKET_SIZE];
	unsigned char* pes_ptr=NULL;
	size_t pes_len;
	size_t count;

	while ( !Interrupted ) {
		
		count = fread(buf, TS_PACKET_SIZE, 1, stream->input);
		if (count==0) break;
		
		// Display the number of packets processed
		fprintf(stderr, "Packets processed: %lu\r", stream->total_packets);
		stream->total_packets++;
		
		// Check the sync-byte
		if (TS_PACKET_SYNC_BYTE(buf) != 0x47) {
			fprintf(stderr,"Error: Lost syncronisation - aborting\n");
			// FIXME: try and regain synchronisation
			break;
		}
		
		// Transport error?
		if ( TS_PACKET_TRANS_ERROR(buf) ) {
			fprintf(stderr, "Warning: Transport error in PID %d.\n", TS_PACKET_PID(buf));
			stream->synced = 0;
			continue;
		}			

		// Scrambled?
		if ( TS_PACKET_SCRAMBLING(buf) ) {
			fprintf(stderr, "Warning: PID %d is scrambled.\n", TS_PACKET_PID(buf));
			continue;
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
		if (stream->pid == 0 && TS_PACKET_PAYLOAD_START(buf)) {

			// Does this one look good ?
			if (TS_PACKET_PAYLOAD_START(buf) && 
			    validate_pes_header( TS_PACKET_PID(buf), pes_ptr, pes_len ))
			{
				// Looks good, use this one
				stream->pid = TS_PACKET_PID(buf);
			}
		}

		// Process the packet, if it is the PID we are interested in		
		if (TS_PACKET_PID(buf) == stream->pid) {

			// Continuity check
			ts_continuity_check( stream, TS_PACKET_CONT_COUNT(buf) );
		
			// Extract PES payload and write it to output
			extract_pes_payload( stream, pes_ptr, pes_len, TS_PACKET_PAYLOAD_START(buf) );
		}

	}

	
}




static void usage()
{
	fprintf( stderr, "Usage: ts2mpa [options] <infile> <outfile>\n" );
	exit(-1);
}


static void termination_handler(int signum)
{
	if (signum==SIGINT) fprintf(stderr, "Recieved SIGINT, aborting.\n");
	else if (signum==SIGTERM) fprintf(stderr, "Recieved SIGTERM, aborting.\n");
	else if (signum==SIGHUP) fprintf(stderr, "Recieved SIGHUP, aborting.\n");
	
	Interrupted = 1;
}


int main( int argc, char** argv )
{
	mpeg_stream_t *stream = calloc( 1, sizeof(mpeg_stream_t) );

	if (argc<3) usage();

	// Configure
	//stream->pid = 436;

	// Open the input file
	stream->input = fopen( argv[1], "rb" );
	if (stream->input==NULL) {
		perror("Failed to open input file");
		exit(-2);
	}

	// Open the output file
	stream->output = fopen( argv[2], "wb" );
	if (stream->output==NULL) {
		perror("Failed to open output file");
		exit(-2);
	}
	
	// Setup signal handling - so we exit cleanly
	if (signal (SIGINT, termination_handler) == SIG_IGN)
		signal (SIGINT, SIG_IGN);
	if (signal (SIGHUP, termination_handler) == SIG_IGN)
		signal (SIGHUP, SIG_IGN);
	if (signal (SIGTERM, termination_handler) == SIG_IGN)
		signal (SIGTERM, SIG_IGN);

	// Hard work happens here
	process_ts_packets( stream );
	
	fprintf(stderr, "\nTotal written: %lu bytes\n", stream->total_bytes);
	
	// Close the input and output files
	fclose( stream->input );
	fclose( stream->output );
	
	free(stream);
	
	// Success
	return 0;
}

