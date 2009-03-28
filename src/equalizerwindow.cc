/***
  This file is part of pavucontrol.

  Copyright 2009 Tanu Kaskinen

  pavucontrol is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 2 of the License, or
  (at your option) any later version.

  pavucontrol is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with pavucontrol. If not, see <http://www.gnu.org/licenses/>.
***/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "equalizerwindow.h"

EqualizerWindow::EqualizerWindow(BaseObjectType* cobject, const Glib::RefPtr<Gnome::Glade::Xml>& x) :
    Gtk::Window(cobject),
    owner(NULL),
    closeButton(NULL) {

    x->get_widget("closeButton", closeButton);

    closeButton->signal_clicked().connect(sigc::mem_fun(*this, &EqualizerWindow::onCloseClicked));
}

EqualizerWindow* EqualizerWindow::create(SinkWidget& owner) {
    EqualizerWindow* w;
    Glib::RefPtr<Gnome::Glade::Xml> x = Gnome::Glade::Xml::create(GLADE_FILE, "equalizerWindow");
    x->get_widget_derived("equalizerWindow", w);
    w->owner = &owner;
    return w;
}

void EqualizerWindow::setSinkDescription(const Glib::ustring& description) {
    set_title("Equalizer for " + description);
}

void EqualizerWindow::onCloseClicked() {
    hide();
}
