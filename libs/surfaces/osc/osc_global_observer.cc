/*
    Copyright (C) 2009 Paul Davis

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

#include "boost/lambda/lambda.hpp"

#include "ardour/session.h"
#include "ardour/route.h"
#include "ardour/audio_track.h"
#include "ardour/midi_track.h"
#include "ardour/dB.h"

#include "osc.h"
#include "osc_global_observer.h"

#include "i18n.h"

using namespace std;
using namespace PBD;
using namespace ARDOUR;
using namespace ArdourSurface;

OSCGlobalObserver::OSCGlobalObserver (Session& s, lo_address a, uint32_t gm)
	: gainmode (gm)
{

	addr = lo_address_new (lo_address_get_hostname(a) , lo_address_get_port(a));

	// connect to all the things we want to send feed back from
	/*
	 * 	Master gain
	 * 		Mute
	 * 		Trim
	 * 		Pan/width
	 * 
	 * 	Monitor
	 * 		Gain
	 * 		Mute
	 * 		Dim
	 * 		Mono
	 * 		Rude Solo
	 * 		etc.
	 * 	Transport
	 * 		Record
	 * 		Play
	 * 		Stop
	 * 		ff/rew
	 * 		punchin/out
	 * 
	 * 	Maybe (many) more
	 */

	// Master channel first, With banking and changes to RID numbering.
	// access by rid = 318 will vanish so this needs to be here.
	// (though it will change to the new way of finding master/monitor)
	boost::shared_ptr<Route> r = s.route_by_remote_id (318);

	boost::shared_ptr<Controllable> mute_controllable = boost::dynamic_pointer_cast<Controllable>(r->mute_control());
	mute_controllable->Changed.connect (mute_changed_connection, MISSING_INVALIDATOR, bind (&OSCGlobalObserver::send_change_message, this, X_("/master/mute"), r->mute_control()), OSC::instance());
	send_change_message ("/master/mute", r->mute_control());

	boost::shared_ptr<Controllable> gain_controllable = boost::dynamic_pointer_cast<Controllable>(r->gain_control());
	if (gainmode) {
		gain_controllable->Changed.connect (gain_changed_connection, MISSING_INVALIDATOR, bind (&OSCGlobalObserver::send_gain_message, this, X_("/master/fader"), r->gain_control()), OSC::instance());
		send_gain_message ("/master/fader", r->gain_control());
	} else {
		gain_controllable->Changed.connect (gain_changed_connection, MISSING_INVALIDATOR, bind (&OSCGlobalObserver::send_gain_message, this, X_("/master/gain"), r->gain_control()), OSC::instance());
		send_gain_message ("/master/gain", r->gain_control());
	}

	// monitor stuff next
	r = s.route_by_remote_id (319);


	// Hmm, it seems the monitor mute is not at route->mute_control()
	boost::shared_ptr<Controllable> mute_controllable2 = boost::dynamic_pointer_cast<Controllable>(r->mute_control());
	//mute_controllable = boost::dynamic_pointer_cast<Controllable>(r2->mute_control());
	mute_controllable2->Changed.connect (monitor_mute_connection, MISSING_INVALIDATOR, bind (&OSCGlobalObserver::send_change_message, this, X_("/monitor/mute"), r->mute_control()), OSC::instance());
	send_change_message ("/monitor/mute", r->mute_control());

	gain_controllable = boost::dynamic_pointer_cast<Controllable>(r->gain_control());
	if (gainmode) {
		gain_controllable->Changed.connect (monitor_gain_connection, MISSING_INVALIDATOR, bind (&OSCGlobalObserver::send_gain_message, this, X_("/monitor/fader"), r->gain_control()), OSC::instance());
		send_gain_message ("/monitor/fader", r->gain_control());
	} else {
		gain_controllable->Changed.connect (monitor_gain_connection, MISSING_INVALIDATOR, bind (&OSCGlobalObserver::send_gain_message, this, X_("/monitor/gain"), r->gain_control()), OSC::instance());
		send_gain_message ("/monitor/gain", r->gain_control());
	}

}

OSCGlobalObserver::~OSCGlobalObserver ()
{
	mute_changed_connection.disconnect();
	gain_changed_connection.disconnect();
	monitor_gain_connection.disconnect();
	monitor_mute_connection.disconnect();

	lo_address_free (addr);
}

void
OSCGlobalObserver::send_change_message (string path, boost::shared_ptr<Controllable> controllable)
{
	lo_message msg = lo_message_new ();

	lo_message_add_float (msg, (float) controllable->get_value());

	/* XXX thread issues */

	//std::cerr << "ORC: send " << path << " = " << controllable->get_value() << std::endl;

	lo_send_message (addr, path.c_str(), msg);
	lo_message_free (msg);
}

void
OSCGlobalObserver::send_gain_message (string path, boost::shared_ptr<Controllable> controllable)
{
	lo_message msg = lo_message_new ();

	if (gainmode) {
		if (controllable->get_value() == 1) {
			lo_message_add_int32 (msg, 800);
		} else {
			lo_message_add_int32 (msg, gain_to_slider_position (controllable->get_value()) * 1023);
		}
	} else {
		if (controllable->get_value() < 1e-15) {
			lo_message_add_float (msg, -200);
		} else {
			lo_message_add_float (msg, accurate_coefficient_to_dB (controllable->get_value()));
		}
	}

	/* XXX thread issues */

	//std::cerr << "ORC: send " << path << " = " << controllable->get_value() << std::endl;

	lo_send_message (addr, path.c_str(), msg);
	lo_message_free (msg);
}

