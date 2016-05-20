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
	session = &s;

	// connect to all the things we want to send feed back from

	/*
	 * 	Master (todo)
	 * 		Trim
	 * 		Pan/width
	 */

	// Master channel first, With banking and changes to RID numbering.
	// access by rid = 318 will vanish so this needs to be here.
	// (though it will change to the new way of finding master/monitor)
	boost::shared_ptr<Route> r = session->route_by_remote_id (318);

	boost::shared_ptr<Controllable> mute_controllable = boost::dynamic_pointer_cast<Controllable>(r->mute_control());
	mute_controllable->Changed.connect (mute_changed_connection, MISSING_INVALIDATOR, bind (&OSCGlobalObserver::send_change_message, this, X_("/master/mute"), r->mute_control()), OSC::instance());
	send_change_message ("/master/mute", r->mute_control());

	boost::shared_ptr<Controllable> trim_controllable = boost::dynamic_pointer_cast<Controllable>(r->trim_control());
		trim_controllable->Changed.connect (trim_changed_connection, MISSING_INVALIDATOR, bind (&OSCGlobalObserver::send_trim_message, this, X_("/master/trimdB"), r->trim_control()), OSC::instance());
		send_trim_message ("/master/trimdB", r->trim_control());

	boost::shared_ptr<Controllable> pan_controllable = boost::dynamic_pointer_cast<Controllable>(r->pan_azimuth_control());
		pan_controllable->Changed.connect (pan_changed_connection, MISSING_INVALIDATOR, bind (&OSCGlobalObserver::send_change_message, this, X_("/master/pan_stereo_position"), r->pan_azimuth_control()), OSC::instance());
		send_change_message ("/master/pan_stereo_position", r->pan_azimuth_control());

	boost::shared_ptr<Controllable> gain_controllable = boost::dynamic_pointer_cast<Controllable>(r->gain_control());
	if (gainmode) {
		gain_controllable->Changed.connect (gain_changed_connection, MISSING_INVALIDATOR, bind (&OSCGlobalObserver::send_gain_message, this, X_("/master/fader"), r->gain_control()), OSC::instance());
		send_gain_message ("/master/fader", r->gain_control());
	} else {
		gain_controllable->Changed.connect (gain_changed_connection, MISSING_INVALIDATOR, bind (&OSCGlobalObserver::send_gain_message, this, X_("/master/gain"), r->gain_control()), OSC::instance());
		send_gain_message ("/master/gain", r->gain_control());
	}

	// monitor stuff next
	/* 
	 * 	Monitor (todo)
	 * 		Mute
	 * 		Dim
	 * 		Mono
	 * 		Rude Solo
	 * 		etc.
	 */
	r = session->route_by_remote_id (319);

	// Hmm, it seems the monitor mute is not at route->mute_control()
	/*boost::shared_ptr<Controllable> mute_controllable2 = boost::dynamic_pointer_cast<Controllable>(r->mute_control());
	//mute_controllable = boost::dynamic_pointer_cast<Controllable>(r2->mute_control());
	mute_controllable2->Changed.connect (monitor_mute_connection, MISSING_INVALIDATOR, bind (&OSCGlobalObserver::send_change_message, this, X_("/monitor/mute"), r->mute_control()), OSC::instance());
	send_change_message ("/monitor/mute", r->mute_control());
	*/
	gain_controllable = boost::dynamic_pointer_cast<Controllable>(r->gain_control());
	if (gainmode) {
		gain_controllable->Changed.connect (monitor_gain_connection, MISSING_INVALIDATOR, bind (&OSCGlobalObserver::send_gain_message, this, X_("/monitor/fader"), r->gain_control()), OSC::instance());
		send_gain_message ("/monitor/fader", r->gain_control());
	} else {
		gain_controllable->Changed.connect (monitor_gain_connection, MISSING_INVALIDATOR, bind (&OSCGlobalObserver::send_gain_message, this, X_("/monitor/gain"), r->gain_control()), OSC::instance());
		send_gain_message ("/monitor/gain", r->gain_control());
	}

	/*
	 * 	Transport (todo)
	 * 		punchin/out
	 */
	 //Transport feedback
	session->TransportStateChange.connect(session_connections, MISSING_INVALIDATOR, boost::bind (&OSCGlobalObserver::send_transport_state_changed, this), OSC::instance());
	send_transport_state_changed ();
	session->RecordStateChanged.connect(session_connections, MISSING_INVALIDATOR, boost::bind (&OSCGlobalObserver::send_record_state_changed, this), OSC::instance());
	send_record_state_changed ();

	/*
	 * 	Maybe (many) more
	 */
}

OSCGlobalObserver::~OSCGlobalObserver ()
{
	mute_changed_connection.disconnect();
	gain_changed_connection.disconnect();
	trim_changed_connection.disconnect();
	pan_changed_connection.disconnect();
	monitor_gain_connection.disconnect();
	monitor_mute_connection.disconnect();
	session_connections.drop_connections ();

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

void
OSCGlobalObserver::send_trim_message (string path, boost::shared_ptr<Controllable> controllable)
{
	lo_message msg = lo_message_new ();

	lo_message_add_float (msg, (float) accurate_coefficient_to_dB (controllable->get_value()));

	lo_send_message (addr, path.c_str(), msg);
	lo_message_free (msg);
}


void
OSCGlobalObserver::send_transport_state_changed()
{

	lo_message msg = lo_message_new ();
	lo_message_add_int32 (msg, session->get_play_loop());
	lo_send_message (addr, "/loop_toggle", msg);
	lo_message_free (msg);

	msg = lo_message_new ();
	lo_message_add_int32 (msg, session->transport_speed() == 1.0);
	lo_send_message (addr, "/transport_play", msg);
	lo_message_free (msg);

	msg = lo_message_new ();
	lo_message_add_int32 (msg, session->transport_stopped ());
	lo_send_message (addr, "/transport_stop", msg);
	lo_message_free (msg);

	msg = lo_message_new ();
	lo_message_add_int32 (msg, session->transport_speed() < 0.0);
	lo_send_message (addr, "/rewind", msg);
	lo_message_free (msg);

	msg = lo_message_new ();
	lo_message_add_int32 (msg, session->transport_speed() > 1.0);
	lo_send_message (addr, "/ffwd", msg);
	lo_message_free (msg);

}

void
OSCGlobalObserver::send_record_state_changed ()
{
	lo_message msg = lo_message_new ();
	lo_message_add_int32 (msg, (int)session->get_record_enabled ());
	lo_send_message (addr, "/rec_enable_toggle", msg);
	lo_message_free (msg);
}

