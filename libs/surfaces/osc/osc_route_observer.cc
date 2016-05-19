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

#include "ardour/route.h"
#include "ardour/audio_track.h"
#include "ardour/midi_track.h"
#include "ardour/dB.h"

#include "osc.h"
#include "osc_route_observer.h"

#include "i18n.h"

using namespace std;
using namespace PBD;
using namespace ARDOUR;
using namespace ArdourSurface;

OSCRouteObserver::OSCRouteObserver (boost::shared_ptr<Route> r, lo_address a, uint32_t s, uint32_t gm, bool m)
	: _route (r)
	,sid (s)
	,gainmode (gm)
	,meter (m)
{

	addr = lo_address_new (lo_address_get_hostname(a) , lo_address_get_port(a));

	_route->PropertyChanged.connect (name_changed_connection, MISSING_INVALIDATOR, boost::bind (&OSCRouteObserver::name_changed, this, boost::lambda::_1), OSC::instance());
	name_changed (ARDOUR::Properties::name);

	if (boost::dynamic_pointer_cast<AudioTrack>(_route) || boost::dynamic_pointer_cast<MidiTrack>(_route)) {

		boost::shared_ptr<Track> track = boost::dynamic_pointer_cast<Track>(r);
		boost::shared_ptr<Controllable> rec_controllable = boost::dynamic_pointer_cast<Controllable>(track->rec_enable_control());

		rec_controllable->Changed.connect (rec_changed_connection, MISSING_INVALIDATOR, bind (&OSCRouteObserver::send_change_message, this, X_("/strip/recenable"), track->rec_enable_control()), OSC::instance());
		send_change_message ("/strip/recenable", track->rec_enable_control());
	}

	boost::shared_ptr<Controllable> mute_controllable = boost::dynamic_pointer_cast<Controllable>(_route->mute_control());
	mute_controllable->Changed.connect (mute_changed_connection, MISSING_INVALIDATOR, bind (&OSCRouteObserver::send_change_message, this, X_("/strip/mute"), _route->mute_control()), OSC::instance());
	send_change_message ("/strip/mute", _route->mute_control());

	boost::shared_ptr<Controllable> solo_controllable = boost::dynamic_pointer_cast<Controllable>(_route->solo_control());
	solo_controllable->Changed.connect (solo_changed_connection, MISSING_INVALIDATOR, bind (&OSCRouteObserver::send_change_message, this, X_("/strip/solo"), _route->solo_control()), OSC::instance());
	_route->listen_changed.connect (solo_changed_connection, MISSING_INVALIDATOR, bind (&OSCRouteObserver::send_change_message, this, X_("/strip/solo"), _route->solo_control()), OSC::instance());
	send_change_message ("/strip/solo", _route->solo_control());

	boost::shared_ptr<Controllable> trim_controllable = boost::dynamic_pointer_cast<Controllable>(_route->trim_control());
		trim_controllable->Changed.connect (trim_changed_connection, MISSING_INVALIDATOR, bind (&OSCRouteObserver::send_trim_message, this, X_("/strip/trimdB"), _route->trim_control()), OSC::instance());
		send_trim_message ("/strip/trimdB", _route->trim_control());

	boost::shared_ptr<Controllable> pan_controllable = boost::dynamic_pointer_cast<Controllable>(_route->pan_azimuth_control());
		pan_controllable->Changed.connect (pan_changed_connection, MISSING_INVALIDATOR, bind (&OSCRouteObserver::send_change_message, this, X_("/strip/pan_stereo_position"), _route->pan_azimuth_control()), OSC::instance());
		send_change_message ("/strip/pan_stereo_position", _route->pan_azimuth_control());

	boost::shared_ptr<Controllable> gain_controllable = boost::dynamic_pointer_cast<Controllable>(_route->gain_control());
	if (gainmode) {
		gain_controllable->Changed.connect (gain_changed_connection, MISSING_INVALIDATOR, bind (&OSCRouteObserver::send_gain_message, this, X_("/strip/fader"), _route->gain_control()), OSC::instance());
		send_gain_message ("/strip/fader", _route->gain_control());
	} else {
		gain_controllable->Changed.connect (gain_changed_connection, MISSING_INVALIDATOR, bind (&OSCRouteObserver::send_gain_message, this, X_("/strip/gain"), _route->gain_control()), OSC::instance());
		send_gain_message ("/strip/gain", _route->gain_control());
	}

	if (meter) {
		// a place to turn metering on for this strip
	}
}

OSCRouteObserver::~OSCRouteObserver ()
{
	name_changed_connection.disconnect();
	rec_changed_connection.disconnect();
	mute_changed_connection.disconnect();
	solo_changed_connection.disconnect();
	gain_changed_connection.disconnect();

	lo_address_free (addr);
}

void
OSCRouteObserver::name_changed (const PBD::PropertyChange& what_changed)
{
	if (!what_changed.contains (ARDOUR::Properties::name)) {
	    return;
	}

	if (!_route) {
		return;
	}

	lo_message msg = lo_message_new ();

	lo_message_add_int32 (msg, sid);
	lo_message_add_string (msg, _route->name().c_str());

	lo_send_message (addr, "/strip/name", msg);
	lo_message_free (msg);
}

void
OSCRouteObserver::send_change_message (string path, boost::shared_ptr<Controllable> controllable)
{
	lo_message msg = lo_message_new ();

	lo_message_add_int32 (msg, sid);

	lo_message_add_float (msg, (float) controllable->get_value());

	/* XXX thread issues */

	//std::cerr << "ORC: send " << path << " = " << controllable->get_value() << std::endl;

	lo_send_message (addr, path.c_str(), msg);
	lo_message_free (msg);
}

void
OSCRouteObserver::send_trim_message (string path, boost::shared_ptr<Controllable> controllable)
{
	lo_message msg = lo_message_new ();

	lo_message_add_int32 (msg, sid);

	lo_message_add_float (msg, (float) accurate_coefficient_to_dB (controllable->get_value()));

	lo_send_message (addr, path.c_str(), msg);
	lo_message_free (msg);
}

void
OSCRouteObserver::send_gain_message (string path, boost::shared_ptr<Controllable> controllable)
{
	lo_message msg = lo_message_new ();

	lo_message_add_int32 (msg, sid);

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

	lo_send_message (addr, path.c_str(), msg);
	lo_message_free (msg);
}
