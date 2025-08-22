#ifndef _ACD_H
#define _ACD_H

#include <driver/network/neighbour.h>
#include <driver/network/protocols/protocols.h>

#define ACD_PROBE_WAIT 1 // initial random delay (1 second)
#define ACD_PROBE_NUM  3 // number of probe packets
#define ACD_PROBE_MIN  1 // minimum delay until repeated probe (1 second)
#define ACD_PROBE_MAX  2 // maximum delay until repeated probe (2 seconds)

#define ACD_ANNOUNCE_WAIT	  2 // delay before announcing (2 seconds)
#define ACD_ANNOUNCE_NUM	  2 // number of announce packets
#define ACD_ANNOUNCE_INTERVAL 2 // time between announce packets (2 seconds)

#define ACD_MAX_CONFLICTS 10 // max conflicts before rate-limiting (10 seconds)
#define ACD_RATE_LIMIT_INTERVAL \
	60 // delay between successive attempts (60 seconds)
#define ACD_DEFEND_INTERVAL \
	10 // minimum interval between defensive ARPs (10 seconds)

ProtocolResult acd_conflict_detected();
void		   acd_probe(NetworkDevice *device);
void		   acd_announce(NetworkDevice *device);

#endif