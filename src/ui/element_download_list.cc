// rTorrent - BitTorrent client
// Copyright (C) 2005-2007, Jari Sundell
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
// 
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
// 
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
//
// In addition, as a special exception, the copyright holders give
// permission to link the code of portions of this program with the
// OpenSSL library under certain conditions as described in each
// individual source file, and distribute linked combinations
// including the two.
//
// You must obey the GNU General Public License in all respects for
// all of the code used other than OpenSSL.  If you modify file(s)
// with this exception, you may extend this exception to your version
// of the file(s), but you are not obligated to do so.  If you do not
// wish to do so, delete this exception statement from your version.
// If you delete this exception statement from all source files in the
// program, then also delete it here.
//
// Contact:  Jari Sundell <jaris@ifi.uio.no>
//
//           Skomakerveien 33
//           3185 Skoppum, NORWAY

#include "config.h"

#include <sigc++/adaptors/bind.h>
#include <torrent/exceptions.h>
#include <torrent/object.h>

#include "core/download.h"
#include "core/manager.h"
#include "core/view.h"
#include "core/view_manager.h"
#include "display/frame.h"
#include "display/manager.h"
#include "input/manager.h"
#include "rpc/parse_commands.h"

#include "control.h"
#include "element_download_list.h"

namespace ui {

ElementDownloadList::ElementDownloadList() :
  m_window(NULL),
  m_view(NULL) {

  receive_change_view("main");

  if (m_view == NULL)
    throw torrent::internal_error("View \"main\" must be present to initialize the main display.");

  m_bindings['\x13'] = sigc::bind(sigc::mem_fun(*this, &ElementDownloadList::receive_command), "d.start=");
  m_bindings['\x04'] = sigc::bind(sigc::mem_fun(*this, &ElementDownloadList::receive_command), "branch=d.get_state=,d.stop=,d.erase=");
  m_bindings['\x0B'] = sigc::bind(sigc::mem_fun(*this, &ElementDownloadList::receive_command), "d.set_ignore_commands=1; d.stop=; d.close=");
  m_bindings['\x12'] = sigc::bind(sigc::mem_fun(*this, &ElementDownloadList::receive_command), "d.check_hash=");
  m_bindings['\x05'] = sigc::bind(sigc::mem_fun(*this, &ElementDownloadList::receive_command),
                                         "f.multicall=,f.set_create_queued=,f.set_resize_queued=; print=\"Queued create/resize of files in torrent.\"");

  m_bindings['+']    = sigc::mem_fun(*this, &ElementDownloadList::receive_next_priority);
  m_bindings['-']    = sigc::mem_fun(*this, &ElementDownloadList::receive_prev_priority);
  m_bindings['T'-'@']= sigc::mem_fun(*this, &ElementDownloadList::receive_cycle_throttle);
  m_bindings['I']    = sigc::bind(sigc::mem_fun(*this, &ElementDownloadList::receive_command),
                                  "branch=d.get_ignore_commands=,"
                                  "{d.set_ignore_commands=0, print=\"Torrent set to heed commands.\"},"
                                  "{d.set_ignore_commands=1, print=\"Torrent set to ignore commands.\"}");
  m_bindings['B'-'@']= sigc::bind(sigc::mem_fun(*this, &ElementDownloadList::receive_command),
                                  "branch=d.is_active=,"
                                  "{print=\"Cannot enable initial seeding on an active download.\"},"
                                  "{d.set_connection_seed=initial_seed, print=\"Enabled initial seeding for the selected download.\"}");

  m_bindings['U']    = sigc::bind(sigc::mem_fun(*this, &ElementDownloadList::receive_command), "d.delete_tied=; print=\"Cleared tied to file association for the selected download.\"");

  // These should also be commands.
  m_bindings['1']           = sigc::bind(sigc::mem_fun(*this, &ElementDownloadList::receive_change_view), "main");
  m_bindings['2']           = sigc::bind(sigc::mem_fun(*this, &ElementDownloadList::receive_change_view), "name");
  m_bindings['3']           = sigc::bind(sigc::mem_fun(*this, &ElementDownloadList::receive_change_view), "started");
  m_bindings['4']           = sigc::bind(sigc::mem_fun(*this, &ElementDownloadList::receive_change_view), "stopped");
  m_bindings['5']           = sigc::bind(sigc::mem_fun(*this, &ElementDownloadList::receive_change_view), "complete");
  m_bindings['6']           = sigc::bind(sigc::mem_fun(*this, &ElementDownloadList::receive_change_view), "incomplete");
  m_bindings['7']           = sigc::bind(sigc::mem_fun(*this, &ElementDownloadList::receive_change_view), "hashing");
  m_bindings['8']           = sigc::bind(sigc::mem_fun(*this, &ElementDownloadList::receive_change_view), "seeding");
  m_bindings['9']           = sigc::bind(sigc::mem_fun(*this, &ElementDownloadList::receive_change_view), "active");

  m_bindings[KEY_UP]   = m_bindings['P' - '@'] = sigc::mem_fun(*this, &ElementDownloadList::receive_prev);
  m_bindings[KEY_DOWN] = m_bindings['N' - '@'] = sigc::mem_fun(*this, &ElementDownloadList::receive_next);
}

void
ElementDownloadList::activate(display::Frame* frame, bool focus) {
  if (is_active())
    throw torrent::internal_error("ui::ElementDownloadList::activate(...) is_active().");

  control->input()->push_back(&m_bindings);

  m_window = new WDownloadList();
  m_window->set_active(true);
  m_window->set_view(m_view);

  m_frame = frame;
  m_frame->initialize_window(m_window);
}

void
ElementDownloadList::disable() {
  if (!is_active())
    throw torrent::internal_error("ui::ElementDownloadList::disable(...) !is_active().");

  control->input()->erase(&m_bindings);

  m_frame->clear();
  m_frame = NULL;

  delete m_window;
  m_window = NULL;
}

void
ElementDownloadList::set_view(core::View* l) {
  m_view = l;
  m_view->sort();

  if (m_window == NULL)
    return;

  m_window->set_view(l);
  m_window->mark_dirty();
}

void
ElementDownloadList::receive_command(const char* cmd) {
  try {
    if (m_view->focus() == m_view->end_visible())
      rpc::parse_command_multiple(rpc::make_target(), cmd, cmd + strlen(cmd));
    else
      rpc::parse_command_multiple(rpc::make_target(*m_view->focus()), cmd, cmd + strlen(cmd));

    m_view->set_last_changed();

  } catch (torrent::input_error& e) {
    control->core()->push_log(e.what());
    return;
  }
}

void
ElementDownloadList::receive_next() {
  m_view->next_focus();
  m_view->set_last_changed();
}

void
ElementDownloadList::receive_prev() {
  m_view->prev_focus();
  m_view->set_last_changed();
}

void
ElementDownloadList::receive_next_priority() {
  if (m_view->focus() == m_view->end_visible())
    return;

  (*m_view->focus())->set_priority((*m_view->focus())->priority() + 1);
  m_window->mark_dirty();
}

void
ElementDownloadList::receive_prev_priority() {
  if (m_view->focus() == m_view->end_visible())
    return;

  (*m_view->focus())->set_priority((*m_view->focus())->priority() - 1);
  m_window->mark_dirty();
}

void
ElementDownloadList::receive_cycle_throttle() {
  if (m_view->focus() == m_view->end_visible())
    return;

  core::Download* download = *m_view->focus();
  if (download->is_active()) {
    control->core()->push_log("Cannot change throttle on active download.");
    return;
  }

  core::ThrottleMap::const_iterator itr = control->core()->throttles().find(download->bencode()->get_key("rtorrent").get_key_string("throttle_name"));
  if (itr == control->core()->throttles().end())
    itr = control->core()->throttles().begin();
  else
    ++itr;

  download->set_throttle_name(itr == control->core()->throttles().end() ? std::string() : itr->first);
  m_window->mark_dirty();
}

void
ElementDownloadList::receive_change_view(const std::string& name) {
  core::ViewManager::iterator itr = control->view_manager()->find(name);

  if (itr == control->view_manager()->end()) {
    control->core()->push_log_std("Could not find view \"" + name + "\".");
    return;
  }

  set_view(*itr);
}

}
