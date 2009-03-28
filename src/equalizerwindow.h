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

#ifndef equalizerwindow_h
#define equalizerwindow_h

#include "pavucontrol.h"

#include "sinkwidget.h"

class EqualizerWindow : public Gtk::Window {
public:
    EqualizerWindow(BaseObjectType* cobject, const Glib::RefPtr<Gnome::Glade::Xml>& x);
    static EqualizerWindow* create(SinkWidget& owner);

    void setSinkDescription(const Glib::ustring& description);

protected:
    virtual void onCloseClicked();

private:
    SinkWidget* owner;
    Gtk::Button* closeButton;
};

#endif
