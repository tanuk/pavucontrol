/***
  This file is part of pavucontrol.

  Copyright 2006-2008 Lennart Poettering
  Copyright 2009 Colin Guthrie

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

#ifndef cardwidget_h
#define cardwidget_h

#include "pavucontrol.h"

class CardWidget : public Gtk::VBox {
public:
    CardWidget(BaseObjectType* cobject, const Glib::RefPtr<Gnome::Glade::Xml>& x);
    static CardWidget* create();

    Gtk::Label *nameLabel;
    Gtk::ToggleButton *streamToggleButton;
    Gtk::Menu menu;
    Gtk::Image *iconImage;
    Glib::ustring name;
    uint32_t index;
    bool updating;

    std::map<Glib::ustring,Glib::ustring> profiles;
    Glib::ustring activeProfile;
    bool hasSinks;
    bool hasSources;

    void prepareMenu();

protected:
  virtual void onProfileChange();

  /* Tree model columns */
  class ModelColumns : public Gtk::TreeModel::ColumnRecord
  {
  public:

    ModelColumns()
    { add(name); add(desc); }

    Gtk::TreeModelColumn<Glib::ustring> name;
    Gtk::TreeModelColumn<Glib::ustring> desc;
  };

  ModelColumns profileModel;

  Gtk::ComboBox *profileList;
  Glib::RefPtr<Gtk::ListStore> treeModel;
};

#endif
