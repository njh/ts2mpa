/* 

	ts2mpa.h
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

#ifndef _TS2MPA_H
#define _TS2MPA_H

#include "mpa_header.h"



typedef struct ts2mpa_s {
	
	FILE* input;
	FILE* output;
	
	int pid;
	int synced;
	int never_synced;
	int continuity_count;
	int pes_stream_id;
	int pes_remaining;
	unsigned long total_bytes;
	unsigned long total_packets;
	
	mpa_header_t mpah;
	
} ts2mpa_t;



// The size of MPEG2 TS packets
#define TS_PACKET_SIZE			188

/*
	Macros for accessing MPEG-2 TS packet headers
*/
#define TS_PACKET_SYNC_BYTE(b)		(b[0])
#define TS_PACKET_TRANS_ERROR(b)	((b[1]&0x80)>>7)
#define TS_PACKET_PAYLOAD_START(b)	((b[1]&0x40)>>6)
#define TS_PACKET_PRIORITY(b)		((b[1]&0x20)>>4)
#define TS_PACKET_PID(b)			(((b[1]&0x1F)<<8) | b[2])

#define TS_PACKET_SCRAMBLING(b)		((b[3]&0xC0)>>6)
#define TS_PACKET_ADAPTATION(b)		((b[3]&0x30)>>4)
#define TS_PACKET_CONT_COUNT(b)		((b[3]&0x0F)>>0)
#define TS_PACKET_ADAPT_LEN(b)		(b[4])



/*
	Macros for accessing MPEG-2 PES packet headers
*/
#define PES_PACKET_SYNC_BYTE1(b)	(b[0])
#define PES_PACKET_SYNC_BYTE2(b)	(b[1])
#define PES_PACKET_SYNC_BYTE3(b)	(b[2])
#define PES_PACKET_STREAM_ID(b)		(b[3])
#define PES_PACKET_LEN(b)			((b[4] << 8) | b[5])

#define PES_PACKET_SYNC_CODE(b)		((b[6] & 0xC0) >> 6)
#define PES_PACKET_SCRAMBLED(b)		((b[6] & 0x30) >> 4)
#define PES_PACKET_PRIORITY(b)		((b[6] & 0x08) >> 3)
#define PES_PACKET_ALIGNMENT(b)		((b[6] & 0x04) >> 2)
#define PES_PACKET_COPYRIGHT(b)		((b[6] & 0x02) >> 1)
#define PES_PACKET_ORIGINAL(b)		((b[6] & 0x01) >> 0)

#define PES_PACKET_PTS_DTS(b)		((b[7] & 0xC0) >> 6)
#define PES_PACKET_ESCR(b)			((b[7] & 0x20) >> 5)
#define PES_PACKET_ESR(b)			((b[7] & 0x10) >> 4)
#define PES_PACKET_DSM_TRICK(b)		((b[7] & 0x8) >> 3)
#define PES_PACKET_ADD_COPY(b)		((b[7] & 0x4) >> 2)
#define PES_PACKET_CRC(b)			((b[7] & 0x2) >> 1)
#define PES_PACKET_EXTEN(b)			((b[7] & 0x1) >> 0)
#define PES_PACKET_HEAD_LEN(b)		(b[8])

#define PES_PACKET_PTS(b)		((uint32_t)((b[9] & 0x0E) << 29) | \
					 (uint32_t)(b[10] << 22) | \
					 (uint32_t)((b[11] & 0xFE) << 14) | \
					 (uint32_t)(b[12] << 7) | \
					 (uint32_t)(b[13] >> 1))

#define PES_PACKET_DTS(b)		((uint32_t)((b[14] & 0x0E) << 29) | \
					 (uint32_t)(b[15] << 22) | \
					 (uint32_t)((b[16] & 0xFE) << 14) | \
					 (uint32_t)(b[17] << 7) | \
					 (uint32_t)(b[18] >> 1))



#endif

