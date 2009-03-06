/***
  This file is part of pavucontrol.

  Copyright 2006-2008 Lennart Poettering
  Copyright 2008 Sjoerd Simons <sjoerd@luon.net>

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

#include <signal.h>
#include <string.h>

#include <gtkmm.h>
#include <libglademm.h>
#include <libintl.h>

#include <canberra-gtk.h>

#include <pulse/pulseaudio.h>
#include <pulse/glib-mainloop.h>
#include <pulse/ext-stream-restore.h>

#include "i18n.h"

#ifndef GLADE_FILE
#define GLADE_FILE "pavucontrol.glade"
#endif

static pa_context *context = NULL;
static int n_outstanding = 0;
static bool show_decibel = true;

enum SinkInputType {
    SINK_INPUT_ALL,
    SINK_INPUT_CLIENT,
    SINK_INPUT_VIRTUAL
};

enum SinkType {
    SINK_ALL,
    SINK_HARDWARE,
    SINK_VIRTUAL,
};

enum SourceOutputType {
    SOURCE_OUTPUT_ALL,
    SOURCE_OUTPUT_CLIENT,
    SOURCE_OUTPUT_VIRTUAL
};

enum SourceType {
    SOURCE_ALL,
    SOURCE_NO_MONITOR,
    SOURCE_HARDWARE,
    SOURCE_VIRTUAL,
    SOURCE_MONITOR,
};

class StreamWidget;
class MainWindow;

class ChannelWidget : public Gtk::EventBox {
public:
    ChannelWidget(BaseObjectType* cobject, const Glib::RefPtr<Gnome::Glade::Xml>& x);
    static ChannelWidget* create();

    void setVolume(pa_volume_t volume);

    Gtk::Label *channelLabel;
    Gtk::Label *volumeLabel;
    Gtk::HScale *volumeScale;

    int channel;
    StreamWidget *streamWidget;

    void onVolumeScaleValueChanged();

    bool can_decibel;
    bool volumeScaleEnabled;

    Glib::ustring beepDevice;

    virtual void set_sensitive(bool enabled);
};

class MinimalStreamWidget : public Gtk::VBox {
public:
    MinimalStreamWidget(BaseObjectType* cobject, const Glib::RefPtr<Gnome::Glade::Xml>& x);

    Gtk::VBox *channelsVBox;
    Gtk::Label *nameLabel, *boldNameLabel;
    Gtk::ToggleButton *streamToggleButton;
    Gtk::Menu menu;
    Gtk::Image *iconImage;
    Gtk::ProgressBar peakProgressBar;
    double lastPeak;

    bool updating;

    void onStreamToggleButton();
    void onMenuDeactivated();
    void popupMenuPosition(int& x, int& y, bool& push_in);

    virtual void prepareMenu(void);

    bool volumeMeterEnabled;
    void enableVolumeMeter();
    void updatePeak(double v);

    Glib::ustring beepDevice;

protected:
    virtual bool on_button_press_event(GdkEventButton* event);
};

class StreamWidget : public MinimalStreamWidget {
public:
    StreamWidget(BaseObjectType* cobject, const Glib::RefPtr<Gnome::Glade::Xml>& x);

    void setChannelMap(const pa_channel_map &m, bool can_decibel);
    void setVolume(const pa_cvolume &volume, bool force);
    virtual void updateChannelVolume(int channel, pa_volume_t v);

    Gtk::ToggleButton *lockToggleButton, *muteToggleButton;

    pa_channel_map channelMap;
    pa_cvolume volume;

    ChannelWidget *channelWidgets[PA_CHANNELS_MAX];

    virtual void onMuteToggleButton();

    sigc::connection timeoutConnection;

    bool timeoutEvent();

    virtual void executeVolumeUpdate();
};

class SinkWidget : public StreamWidget {
public:
    SinkWidget(BaseObjectType* cobject, const Glib::RefPtr<Gnome::Glade::Xml>& x);
    static SinkWidget* create();

    SinkType type;
    Glib::ustring description;
    Glib::ustring name;
    uint32_t index, monitor_index;
    bool can_decibel;

    Gtk::CheckMenuItem defaultMenuItem;
    Gtk::MenuItem equalizationMenuItem;

    virtual void onMuteToggleButton();
    virtual void executeVolumeUpdate();
    virtual void onDefaultToggle();
    virtual void onEqualizationActivate();
};

class SourceWidget : public StreamWidget {
public:
    SourceWidget(BaseObjectType* cobject, const Glib::RefPtr<Gnome::Glade::Xml>& x);
    static SourceWidget* create();

    SourceType type;
    Glib::ustring name;
    Glib::ustring description;
    uint32_t index;
    bool can_decibel;

    Gtk::CheckMenuItem defaultMenuItem;

    virtual void onMuteToggleButton();
    virtual void executeVolumeUpdate();
    virtual void onDefaultToggle();
};

class SinkInputWidget : public StreamWidget {
public:
    SinkInputWidget(BaseObjectType* cobject, const Glib::RefPtr<Gnome::Glade::Xml>& x);
    static SinkInputWidget* create();
    virtual ~SinkInputWidget();

    SinkInputType type;

    uint32_t index, clientIndex, sinkIndex;
    virtual void executeVolumeUpdate();
    virtual void onMuteToggleButton();
    virtual void onKill();
    virtual void prepareMenu();

    MainWindow *mainWindow;
    Gtk::Menu submenu;
    Gtk::MenuItem titleMenuItem, killMenuItem;

    struct SinkMenuItem {
        SinkMenuItem(SinkInputWidget *w, const char *label, uint32_t i, bool active) :
            widget(w),
            menuItem(label),
            index(i) {
            menuItem.set_active(active);
            menuItem.set_draw_as_radio(true);
            menuItem.signal_toggled().connect(sigc::mem_fun(*this, &SinkMenuItem::onToggle));
        }

        SinkInputWidget *widget;
        Gtk::CheckMenuItem menuItem;
        uint32_t index;
        void onToggle();
    };

    std::map<uint32_t, SinkMenuItem*> sinkMenuItems;

    void clearMenu();
    void buildMenu();
};

class SourceOutputWidget : public MinimalStreamWidget {
public:
    SourceOutputWidget(BaseObjectType* cobject, const Glib::RefPtr<Gnome::Glade::Xml>& x);
    static SourceOutputWidget* create();
    virtual ~SourceOutputWidget();

    SourceOutputType type;

    uint32_t index, clientIndex, sourceIndex;
    virtual void onKill();

    MainWindow *mainWindow;
    Gtk::Menu submenu;
    Gtk::MenuItem titleMenuItem, killMenuItem;

    struct SourceMenuItem {
        SourceMenuItem(SourceOutputWidget *w, const char *label, uint32_t i, bool active) :
            widget(w),
            menuItem(label),
            index(i) {
            menuItem.set_active(active);
            menuItem.set_draw_as_radio(true);
            menuItem.signal_toggled().connect(sigc::mem_fun(*this, &SourceMenuItem::onToggle));
        }

        SourceOutputWidget *widget;
        Gtk::CheckMenuItem menuItem;
        uint32_t index;
        void onToggle();
    };

    std::map<uint32_t, SourceMenuItem*> sourceMenuItems;

    void clearMenu();
    void buildMenu();
    virtual void prepareMenu();
};

class RoleWidget : public StreamWidget {
public:
    RoleWidget(BaseObjectType* cobject, const Glib::RefPtr<Gnome::Glade::Xml>& x);
    static RoleWidget* create();

    Glib::ustring role;
    Glib::ustring device;

    virtual void onMuteToggleButton();
    virtual void executeVolumeUpdate();
};

class MainWindow : public Gtk::Window {
public:
    MainWindow(BaseObjectType* cobject, const Glib::RefPtr<Gnome::Glade::Xml>& x);
    static MainWindow* create();
    virtual ~MainWindow();

    void updateSink(const pa_sink_info &info);
    void updateSource(const pa_source_info &info);
    void updateSinkInput(const pa_sink_input_info &info);
    void updateSourceOutput(const pa_source_output_info &info);
    void updateClient(const pa_client_info &info);
    void updateServer(const pa_server_info &info);
    void updateVolumeMeter(uint32_t source_index, uint32_t sink_input_index, double v);
    void updateRole(const pa_ext_stream_restore_info &info);

    void removeSink(uint32_t index);
    void removeSource(uint32_t index);
    void removeSinkInput(uint32_t index);
    void removeSourceOutput(uint32_t index);
    void removeClient(uint32_t index);

    Gtk::Notebook *notebook;
    Gtk::VBox *streamsVBox, *recsVBox, *sinksVBox, *sourcesVBox;
    Gtk::Label *noStreamsLabel, *noRecsLabel, *noSinksLabel, *noSourcesLabel;
    Gtk::ComboBox *sinkInputTypeComboBox, *sourceOutputTypeComboBox, *sinkTypeComboBox, *sourceTypeComboBox;

    std::map<uint32_t, SinkWidget*> sinkWidgets;
    std::map<uint32_t, SourceWidget*> sourceWidgets;
    std::map<uint32_t, SinkInputWidget*> sinkInputWidgets;
    std::map<uint32_t, SourceOutputWidget*> sourceOutputWidgets;
    std::map<uint32_t, char*> clientNames;

    SinkInputType showSinkInputType;
    SinkType showSinkType;
    SourceOutputType showSourceOutputType;
    SourceType showSourceType;

    virtual void onSinkInputTypeComboBoxChanged();
    virtual void onSourceOutputTypeComboBoxChanged();
    virtual void onSinkTypeComboBoxChanged();
    virtual void onSourceTypeComboBoxChanged();

    void updateDeviceVisibility();
    void reallyUpdateDeviceVisibility();
    void createMonitorStreamForSource(uint32_t source_idx);
    void createMonitorStreamForSinkInput(uint32_t sink_input_idx, uint32_t sink_idx);

    void setIconFromProplist(Gtk::Image *icon, pa_proplist *l, const char *name);

    RoleWidget *eventRoleWidget;

    bool createEventRoleWidget();
    void deleteEventRoleWidget();

    Glib::ustring defaultSinkName, defaultSourceName;

protected:
    virtual void on_realize();
};

void show_error(const char *txt) {
    char buf[256];

    snprintf(buf, sizeof(buf), "%s: %s", txt, pa_strerror(pa_context_errno(context)));

    Gtk::MessageDialog dialog(buf, false, Gtk::MESSAGE_ERROR, Gtk::BUTTONS_CLOSE, true);
    dialog.run();

    Gtk::Main::quit();
}

/*** ChannelWidget ***/

ChannelWidget::ChannelWidget(BaseObjectType* cobject, const Glib::RefPtr<Gnome::Glade::Xml>& x) :
    Gtk::EventBox(cobject),
    volumeScaleEnabled(true) {

    x->get_widget("channelLabel", channelLabel);
    x->get_widget("volumeLabel", volumeLabel);
    x->get_widget("volumeScale", volumeScale);

    volumeScale->set_value(100);

    volumeScale->signal_value_changed().connect(sigc::mem_fun(*this, &ChannelWidget::onVolumeScaleValueChanged));
}

ChannelWidget* ChannelWidget::create() {
    ChannelWidget* w;
    Glib::RefPtr<Gnome::Glade::Xml> x = Gnome::Glade::Xml::create(GLADE_FILE, "channelWidget");
    x->get_widget_derived("channelWidget", w);
    return w;
}

void ChannelWidget::setVolume(pa_volume_t volume) {
    double v;
    char txt[64];

    v = ((gdouble) volume * 100) / PA_VOLUME_NORM;

    if (can_decibel && show_decibel) {
        double dB = pa_sw_volume_to_dB(volume);

        if (dB > PA_DECIBEL_MININFTY) {
            snprintf(txt, sizeof(txt), "%0.2f dB", dB);
            volumeLabel->set_tooltip_text(txt);
        } else
            volumeLabel->set_tooltip_markup("-&#8734;dB");
        volumeLabel->set_has_tooltip(TRUE);
    } else
        volumeLabel->set_has_tooltip(FALSE);

    snprintf(txt, sizeof(txt), "%0.0f%%", v);
    volumeLabel->set_text(txt);

    volumeScaleEnabled = false;
    volumeScale->set_value(v > 100 ? 100 : v);
    volumeScaleEnabled = true;
}

void ChannelWidget::onVolumeScaleValueChanged() {

    if (!volumeScaleEnabled)
        return;

    if (streamWidget->updating)
        return;

    pa_volume_t volume = (pa_volume_t) ((volumeScale->get_value() * PA_VOLUME_NORM) / 100);
    streamWidget->updateChannelVolume(channel, volume);

    if (beepDevice != "") {
        ca_context_change_device(ca_gtk_context_get(), beepDevice.c_str());

        ca_context_cancel(ca_gtk_context_get(), 2);

        int r = ca_gtk_play_for_widget(GTK_WIDGET(volumeScale->gobj()),
                               2,
                               CA_PROP_EVENT_DESCRIPTION, _("Volume Control Feedback Sound"),
                               CA_PROP_EVENT_ID, "audio-volume-change",
                               CA_PROP_CANBERRA_CACHE_CONTROL, "permanent",
                               CA_PROP_CANBERRA_VOLUME, "0",
                               CA_PROP_CANBERRA_ENABLE, "1",
                               NULL);

        ca_context_change_device(ca_gtk_context_get(), NULL);
    }
}

void ChannelWidget::set_sensitive(bool enabled) {
    Gtk::EventBox::set_sensitive(enabled);

    channelLabel->set_sensitive(enabled);
    volumeLabel->set_sensitive(enabled);
    volumeScale->set_sensitive(enabled);
}

/*** MinimalStreamWidget ***/

MinimalStreamWidget::MinimalStreamWidget(BaseObjectType* cobject, const Glib::RefPtr<Gnome::Glade::Xml>& x) :
    Gtk::VBox(cobject),
    peakProgressBar(),
    lastPeak(0),
    updating(false),
    volumeMeterEnabled(false) {

    x->get_widget("channelsVBox", channelsVBox);
    x->get_widget("nameLabel", nameLabel);
    x->get_widget("boldNameLabel", boldNameLabel);
    x->get_widget("streamToggle", streamToggleButton);
    x->get_widget("iconImage", iconImage);

    peakProgressBar.set_size_request(-1, 10);
    channelsVBox->pack_end(peakProgressBar, false, false);

    streamToggleButton->set_active(false);
    streamToggleButton->signal_clicked().connect(sigc::mem_fun(*this, &MinimalStreamWidget::onStreamToggleButton));
    menu.signal_deactivate().connect(sigc::mem_fun(*this, &MinimalStreamWidget::onMenuDeactivated));

    peakProgressBar.hide();
}

void MinimalStreamWidget::prepareMenu(void) {
}

void MinimalStreamWidget::onMenuDeactivated(void) {
    streamToggleButton->set_active(false);
}

void MinimalStreamWidget::popupMenuPosition(int& x, int& y, bool& push_in G_GNUC_UNUSED) {
    Gtk::Requisition  r;

    streamToggleButton->get_window()->get_origin(x, y);
    r = menu.size_request();

    /* Align the right side of the menu with the right side of the togglebutton */
    x += streamToggleButton->get_allocation().get_x();
    x += streamToggleButton->get_allocation().get_width();
    x -= r.width;

    /* Align the top of the menu with the buttom of the togglebutton */
    y += streamToggleButton->get_allocation().get_y();
    y += streamToggleButton->get_allocation().get_height();
}

void MinimalStreamWidget::onStreamToggleButton(void) {
    if (streamToggleButton->get_active()) {
        prepareMenu();
        menu.popup(sigc::mem_fun(*this, &MinimalStreamWidget::popupMenuPosition), 0, gtk_get_current_event_time());
    }
}

bool MinimalStreamWidget::on_button_press_event (GdkEventButton* event) {
    if (Gtk::VBox::on_button_press_event(event))
        return TRUE;

    if (event->type == GDK_BUTTON_PRESS && event->button == 3) {
        prepareMenu();
        menu.popup(0, event->time);
        return TRUE;
    }

    return FALSE;
}

#define DECAY_STEP .04

void MinimalStreamWidget::updatePeak(double v) {

    if (lastPeak >= DECAY_STEP)
        if (v < lastPeak - DECAY_STEP)
            v = lastPeak - DECAY_STEP;

    lastPeak = v;

    if (v >= 0) {
        peakProgressBar.set_sensitive(TRUE);
        peakProgressBar.set_fraction(v);
    } else {
        peakProgressBar.set_sensitive(FALSE);
        peakProgressBar.set_fraction(0);
    }

    enableVolumeMeter();
}

void MinimalStreamWidget::enableVolumeMeter() {
    if (volumeMeterEnabled)
        return;

    volumeMeterEnabled = true;
    peakProgressBar.show();
}

/*** StreamWidget ***/

StreamWidget::StreamWidget(BaseObjectType* cobject, const Glib::RefPtr<Gnome::Glade::Xml>& x) :
    MinimalStreamWidget(cobject, x)  {

    x->get_widget("lockToggleButton", lockToggleButton);
    x->get_widget("muteToggleButton", muteToggleButton);

    muteToggleButton->signal_clicked().connect(sigc::mem_fun(*this, &StreamWidget::onMuteToggleButton));

    for (unsigned i = 0; i < PA_CHANNELS_MAX; i++)
        channelWidgets[i] = NULL;
}

void StreamWidget::setChannelMap(const pa_channel_map &m, bool can_decibel) {
    channelMap = m;

    for (int i = 0; i < m.channels; i++) {
        ChannelWidget *cw = channelWidgets[i] = ChannelWidget::create();
        cw->beepDevice = beepDevice;
        cw->channel = i;
        cw->can_decibel = can_decibel;
        cw->streamWidget = this;
        char text[64];
        snprintf(text, sizeof(text), "<b>%s</b>", pa_channel_position_to_pretty_string(m.map[i]));
        cw->channelLabel->set_markup(text);
        channelsVBox->pack_start(*cw, false, false, 0);
    }

    lockToggleButton->set_sensitive(m.channels > 1);
}

void StreamWidget::setVolume(const pa_cvolume &v, bool force = false) {
    g_assert(v.channels == channelMap.channels);

    volume = v;

    if (timeoutConnection.empty() || force) { /* do not update the volume when a volume change is still in flux */
        for (int i = 0; i < volume.channels; i++)
            channelWidgets[i]->setVolume(volume.values[i]);
    }
}

void StreamWidget::updateChannelVolume(int channel, pa_volume_t v) {
    pa_cvolume n;
    g_assert(channel < volume.channels);

    n = volume;
    if (lockToggleButton->get_active()) {
        for (int i = 0; i < n.channels; i++)
            n.values[i] = v;
    } else
        n.values[channel] = v;

    setVolume(n, true);

    if (timeoutConnection.empty())
        timeoutConnection = Glib::signal_timeout().connect(sigc::mem_fun(*this, &StreamWidget::timeoutEvent), 100);
}

void StreamWidget::onMuteToggleButton() {

    lockToggleButton->set_sensitive(!muteToggleButton->get_active());

    for (int i = 0; i < channelMap.channels; i++)
        channelWidgets[i]->set_sensitive(!muteToggleButton->get_active());
}

bool StreamWidget::timeoutEvent() {
    executeVolumeUpdate();
    return false;
}

void StreamWidget::executeVolumeUpdate() {
}

/*** SinkWidget ***/

SinkWidget::SinkWidget(BaseObjectType* cobject, const Glib::RefPtr<Gnome::Glade::Xml>& x) :
    StreamWidget(cobject, x),
    defaultMenuItem("_Default", true),
    equalizationMenuItem("_Equalization...", true) {

    add_events(Gdk::BUTTON_PRESS_MASK);

    defaultMenuItem.set_active(false);
    defaultMenuItem.signal_toggled().connect(sigc::mem_fun(*this, &SinkWidget::onDefaultToggle));
    
    equalizationMenuItem.signal_activate().connect(sigc::mem_fun(*this, &SinkWidget::onEqualizationActivate));
    
    menu.append(defaultMenuItem);
    menu.append(equalizationMenuItem);
    menu.show_all();
}

SinkWidget* SinkWidget::create() {
    SinkWidget* w;
    Glib::RefPtr<Gnome::Glade::Xml> x = Gnome::Glade::Xml::create(GLADE_FILE, "streamWidget");
    x->get_widget_derived("streamWidget", w);
    return w;
}

void SinkWidget::executeVolumeUpdate() {
    pa_operation* o;

    if (!(o = pa_context_set_sink_volume_by_index(context, index, &volume, NULL, NULL))) {
        show_error(_("pa_context_set_sink_volume_by_index() failed"));
        return;
    }

    pa_operation_unref(o);
}

void SinkWidget::onMuteToggleButton() {
    StreamWidget::onMuteToggleButton();

    if (updating)
        return;

    pa_operation* o;
    if (!(o = pa_context_set_sink_mute_by_index(context, index, muteToggleButton->get_active(), NULL, NULL))) {
        show_error(_("pa_context_set_sink_mute_by_index() failed"));
        return;
    }

    pa_operation_unref(o);
}

void SinkWidget::onDefaultToggle() {
    pa_operation* o;

    if (updating)
        return;

    if (!(o = pa_context_set_default_sink(context, name.c_str(), NULL, NULL))) {
        show_error(_("pa_context_set_default_sink() failed"));
        return;
    }
    pa_operation_unref(o);
}

void SinkWidget::onEqualizationActivate() {
    g_message("Equalization controlling is not implemented yet.");
}

/*** SourceWidget ***/

SourceWidget::SourceWidget(BaseObjectType* cobject, const Glib::RefPtr<Gnome::Glade::Xml>& x) :
    StreamWidget(cobject, x),
    defaultMenuItem(_("_Default"), true){

    add_events(Gdk::BUTTON_PRESS_MASK);

    defaultMenuItem.set_active(false);
    defaultMenuItem.signal_toggled().connect(sigc::mem_fun(*this, &SourceWidget::onDefaultToggle));
    menu.append(defaultMenuItem);
    menu.show_all();
}

SourceWidget* SourceWidget::create() {
    SourceWidget* w;
    Glib::RefPtr<Gnome::Glade::Xml> x = Gnome::Glade::Xml::create(GLADE_FILE, "streamWidget");
    x->get_widget_derived("streamWidget", w);
    return w;
}

void SourceWidget::executeVolumeUpdate() {
    pa_operation* o;

    if (!(o = pa_context_set_source_volume_by_index(context, index, &volume, NULL, NULL))) {
        show_error(_("pa_context_set_source_volume_by_index() failed"));
        return;
    }

    pa_operation_unref(o);
}

void SourceWidget::onMuteToggleButton() {
    StreamWidget::onMuteToggleButton();

    if (updating)
        return;

    pa_operation* o;
    if (!(o = pa_context_set_source_mute_by_index(context, index, muteToggleButton->get_active(), NULL, NULL))) {
        show_error(_("pa_context_set_source_mute_by_index() failed"));
        return;
    }

    pa_operation_unref(o);
}

void SourceWidget::onDefaultToggle() {
    pa_operation* o;

    if (updating)
        return;

    if (!(o = pa_context_set_default_source(context, name.c_str(), NULL, NULL))) {
        show_error(_("pa_context_set_default_source() failed"));
        return;
    }
    pa_operation_unref(o);
}

/*** SinkInputWidget ***/

SinkInputWidget::SinkInputWidget(BaseObjectType* cobject, const Glib::RefPtr<Gnome::Glade::Xml>& x) :
    StreamWidget(cobject, x),
    mainWindow(NULL),
    titleMenuItem(_("_Move Stream..."), true),
    killMenuItem(_("_Terminate Stream"), true) {

    add_events(Gdk::BUTTON_PRESS_MASK);

    menu.append(titleMenuItem);
    titleMenuItem.set_submenu(submenu);

    menu.append(killMenuItem);
    killMenuItem.signal_activate().connect(sigc::mem_fun(*this, &SinkInputWidget::onKill));
}

SinkInputWidget::~SinkInputWidget() {
    clearMenu();
}

SinkInputWidget* SinkInputWidget::create() {
    SinkInputWidget* w;
    Glib::RefPtr<Gnome::Glade::Xml> x = Gnome::Glade::Xml::create(GLADE_FILE, "streamWidget");
    x->get_widget_derived("streamWidget", w);
    return w;
}

void SinkInputWidget::executeVolumeUpdate() {
    pa_operation* o;

    if (!(o = pa_context_set_sink_input_volume(context, index, &volume, NULL, NULL))) {
        show_error(_("pa_context_set_sink_input_volume() failed"));
        return;
    }

    pa_operation_unref(o);
}

void SinkInputWidget::onMuteToggleButton() {
    StreamWidget::onMuteToggleButton();

    if (updating)
        return;

    pa_operation* o;
    if (!(o = pa_context_set_sink_input_mute(context, index, muteToggleButton->get_active(), NULL, NULL))) {
        show_error(_("pa_context_set_sink_input_mute() failed"));
        return;
    }

    pa_operation_unref(o);
}

void SinkInputWidget::prepareMenu() {
  clearMenu();
  buildMenu();
}

void SinkInputWidget::clearMenu() {

    while (!sinkMenuItems.empty()) {
        std::map<uint32_t, SinkMenuItem*>::iterator i = sinkMenuItems.begin();
        delete i->second;
        sinkMenuItems.erase(i);
    }
}

void SinkInputWidget::buildMenu() {
    for (std::map<uint32_t, SinkWidget*>::iterator i = mainWindow->sinkWidgets.begin(); i != mainWindow->sinkWidgets.end(); ++i) {
        SinkMenuItem *m;
        sinkMenuItems[i->second->index] = m = new SinkMenuItem(this, i->second->description.c_str(), i->second->index, i->second->index == sinkIndex);
        submenu.append(m->menuItem);
    }

    menu.show_all();
}

void SinkInputWidget::onKill() {
    pa_operation* o;
    if (!(o = pa_context_kill_sink_input(context, index, NULL, NULL))) {
        show_error(_("pa_context_kill_sink_input() failed"));
        return;
    }

    pa_operation_unref(o);
}

void SinkInputWidget::SinkMenuItem::onToggle() {

    if (widget->updating)
        return;

    if (!menuItem.get_active())
        return;

    pa_operation* o;
    if (!(o = pa_context_move_sink_input_by_index(context, widget->index, index, NULL, NULL))) {
        show_error(_("pa_context_move_sink_input_by_index() failed"));
        return;
    }

    pa_operation_unref(o);
}

/*** SourceOutputWidget ***/

SourceOutputWidget::SourceOutputWidget(BaseObjectType* cobject, const Glib::RefPtr<Gnome::Glade::Xml>& x) :
    MinimalStreamWidget(cobject, x),
    mainWindow(NULL),
    titleMenuItem(_("_Move Stream..."), true),
    killMenuItem(_("_Terminate Stream"), true) {

    add_events(Gdk::BUTTON_PRESS_MASK);

    menu.append(titleMenuItem);
    titleMenuItem.set_submenu(submenu);

    menu.append(killMenuItem);
    killMenuItem.signal_activate().connect(sigc::mem_fun(*this, &SourceOutputWidget::onKill));
}

SourceOutputWidget::~SourceOutputWidget() {
    clearMenu();
}

SourceOutputWidget* SourceOutputWidget::create() {
    SourceOutputWidget* w;
    Glib::RefPtr<Gnome::Glade::Xml> x = Gnome::Glade::Xml::create(GLADE_FILE, "streamWidget");
    x->get_widget_derived("streamWidget", w);
    return w;
}

void SourceOutputWidget::onKill() {
    pa_operation* o;
    if (!(o = pa_context_kill_source_output(context, index, NULL, NULL))) {
        show_error(_("pa_context_kill_source_output() failed"));
        return;
    }

    pa_operation_unref(o);
}

void SourceOutputWidget::clearMenu() {

    while (!sourceMenuItems.empty()) {
        std::map<uint32_t, SourceMenuItem*>::iterator i = sourceMenuItems.begin();
        delete i->second;
        sourceMenuItems.erase(i);
    }
}

void SourceOutputWidget::buildMenu() {
    for (std::map<uint32_t, SourceWidget*>::iterator i = mainWindow->sourceWidgets.begin(); i != mainWindow->sourceWidgets.end(); ++i) {
        SourceMenuItem *m;
        sourceMenuItems[i->second->index] = m = new SourceMenuItem(this, i->second->description.c_str(), i->second->index, i->second->index == sourceIndex);
        submenu.append(m->menuItem);
    }

    menu.show_all();
}

void SourceOutputWidget::prepareMenu(void) {
  clearMenu();
  buildMenu();
}

void SourceOutputWidget::SourceMenuItem::onToggle() {

    if (widget->updating)
        return;

    if (!menuItem.get_active())
        return;

    pa_operation* o;
    if (!(o = pa_context_move_source_output_by_index(context, widget->index, index, NULL, NULL))) {
        show_error(_("pa_context_move_source_output_by_index() failed"));
        return;
    }

    pa_operation_unref(o);
}

/*** RoleWidget ***/

RoleWidget::RoleWidget(BaseObjectType* cobject, const Glib::RefPtr<Gnome::Glade::Xml>& x) :
    StreamWidget(cobject, x) {

    lockToggleButton->hide();
    streamToggleButton->hide();
}

RoleWidget* RoleWidget::create() {
    RoleWidget* w;
    Glib::RefPtr<Gnome::Glade::Xml> x = Gnome::Glade::Xml::create(GLADE_FILE, "streamWidget");
    x->get_widget_derived("streamWidget", w);
    return w;
}

void RoleWidget::onMuteToggleButton() {
    StreamWidget::onMuteToggleButton();

    executeVolumeUpdate();
}

void RoleWidget::executeVolumeUpdate() {
    pa_ext_stream_restore_info info;

    if (updating)
        return;

    info.name = role.c_str();
    info.channel_map.channels = 1;
    info.channel_map.map[0] = PA_CHANNEL_POSITION_MONO;
    info.volume = volume;
    info.device = device == "" ? NULL : device.c_str();
    info.mute = muteToggleButton->get_active();

    pa_operation* o;
    if (!(o = pa_ext_stream_restore_write(context, PA_UPDATE_REPLACE, &info, 1, TRUE, NULL, NULL))) {
        show_error(_("pa_ext_stream_restore_write() failed"));
        return;
    }

    pa_operation_unref(o);
}

/*** MainWindow ***/

MainWindow::MainWindow(BaseObjectType* cobject, const Glib::RefPtr<Gnome::Glade::Xml>& x) :
    Gtk::Window(cobject),
    showSinkInputType(SINK_INPUT_CLIENT),
    showSinkType(SINK_ALL),
    showSourceOutputType(SOURCE_OUTPUT_CLIENT),
    showSourceType(SOURCE_NO_MONITOR),
    eventRoleWidget(NULL){

    x->get_widget("streamsVBox", streamsVBox);
    x->get_widget("recsVBox", recsVBox);
    x->get_widget("sinksVBox", sinksVBox);
    x->get_widget("sourcesVBox", sourcesVBox);
    x->get_widget("noStreamsLabel", noStreamsLabel);
    x->get_widget("noRecsLabel", noRecsLabel);
    x->get_widget("noSinksLabel", noSinksLabel);
    x->get_widget("noSourcesLabel", noSourcesLabel);
    x->get_widget("sinkInputTypeComboBox", sinkInputTypeComboBox);
    x->get_widget("sourceOutputTypeComboBox", sourceOutputTypeComboBox);
    x->get_widget("sinkTypeComboBox", sinkTypeComboBox);
    x->get_widget("sourceTypeComboBox", sourceTypeComboBox);
    x->get_widget("notebook", notebook);

    sourcesVBox->set_reallocate_redraws(true);
    streamsVBox->set_reallocate_redraws(true);
    recsVBox->set_reallocate_redraws(true);
    sinksVBox->set_reallocate_redraws(true);

    sinkInputTypeComboBox->set_active((int) showSinkInputType);
    sourceOutputTypeComboBox->set_active((int) showSourceOutputType);
    sinkTypeComboBox->set_active((int) showSinkType);
    sourceTypeComboBox->set_active((int) showSourceType);

    sinkInputTypeComboBox->signal_changed().connect(sigc::mem_fun(*this, &MainWindow::onSinkInputTypeComboBoxChanged));
    sourceOutputTypeComboBox->signal_changed().connect(sigc::mem_fun(*this, &MainWindow::onSourceOutputTypeComboBoxChanged));
    sinkTypeComboBox->signal_changed().connect(sigc::mem_fun(*this, &MainWindow::onSinkTypeComboBoxChanged));
    sourceTypeComboBox->signal_changed().connect(sigc::mem_fun(*this, &MainWindow::onSourceTypeComboBoxChanged));
}

MainWindow* MainWindow::create() {
    MainWindow* w;
    Glib::RefPtr<Gnome::Glade::Xml> x = Gnome::Glade::Xml::create(GLADE_FILE, "mainWindow");
    x->get_widget_derived("mainWindow", w);
    return w;
}

void MainWindow::on_realize() {
    Gtk::Window::on_realize();

    get_window()->set_cursor(Gdk::Cursor(Gdk::WATCH));
}

MainWindow::~MainWindow() {
    while (!clientNames.empty()) {
        std::map<uint32_t, char*>::iterator i = clientNames.begin();
        g_free(i->second);
        clientNames.erase(i);
    }
}

void MainWindow::updateSink(const pa_sink_info &info) {
    SinkWidget *w;
    bool is_new = false;

    if (sinkWidgets.count(info.index))
        w = sinkWidgets[info.index];
    else {
        sinkWidgets[info.index] = w = SinkWidget::create();
        w->beepDevice = info.name;
        w->setChannelMap(info.channel_map, !!(info.flags & PA_SINK_DECIBEL_VOLUME));
        sinksVBox->pack_start(*w, false, false, 0);
        w->index = info.index;
        w->monitor_index = info.monitor_source;
        is_new = true;
    }

    w->updating = true;

    w->name = info.name;
    w->description = info.description;
    w->type = info.flags & PA_SINK_HARDWARE ? SINK_HARDWARE : SINK_VIRTUAL;

    w->boldNameLabel->set_text("");
    gchar *txt;
    w->nameLabel->set_markup(txt = g_markup_printf_escaped("%s", info.description));
    g_free(txt);

    w->iconImage->set_from_icon_name("audio-card", Gtk::ICON_SIZE_SMALL_TOOLBAR);

    w->setVolume(info.volume);
    w->muteToggleButton->set_active(info.mute);

    w->defaultMenuItem.set_active(w->name == defaultSinkName);

    w->updating = false;

    if (is_new)
        updateDeviceVisibility();
}

static void suspended_callback(pa_stream *s, void *userdata) {
    MainWindow *w = static_cast<MainWindow*>(userdata);

    if (pa_stream_is_suspended(s))
        w->updateVolumeMeter(pa_stream_get_device_index(s), PA_INVALID_INDEX, -1);
}

static void read_callback(pa_stream *s, size_t length, void *userdata) {
    MainWindow *w = static_cast<MainWindow*>(userdata);
    const void *data;
    double v;

    if (pa_stream_peek(s, &data, &length) < 0) {
        show_error(_("Failed to read data from stream"));
        return;
    }

    assert(length > 0);
    assert(length % sizeof(float) == 0);

    v = ((const float*) data)[length / sizeof(float) -1];

    pa_stream_drop(s);

    if (v < 0)
        v = 0;
    if (v > 1)
        v = 1;

    w->updateVolumeMeter(pa_stream_get_device_index(s), pa_stream_get_monitor_stream(s), v);
}

void MainWindow::createMonitorStreamForSource(uint32_t source_idx) {
    pa_stream *s;
    char t[16];
    pa_buffer_attr attr;
    pa_sample_spec ss;

    ss.channels = 1;
    ss.format = PA_SAMPLE_FLOAT32;
    ss.rate = 25;

    memset(&attr, 0, sizeof(attr));
    attr.fragsize = sizeof(float);
    attr.maxlength = (uint32_t) -1;

    snprintf(t, sizeof(t), "%u", source_idx);

    if (!(s = pa_stream_new(context, _("Peak detect"), &ss, NULL))) {
        show_error(_("Failed to create monitoring stream"));
        return;
    }

    pa_stream_set_read_callback(s, read_callback, this);
    pa_stream_set_suspended_callback(s, suspended_callback, this);

    if (pa_stream_connect_record(s, t, &attr, (pa_stream_flags_t) (PA_STREAM_DONT_MOVE|PA_STREAM_PEAK_DETECT|PA_STREAM_ADJUST_LATENCY)) < 0) {
        show_error(_("Failed to connect monitoring stream"));
        pa_stream_unref(s);
        return;
    }
}

void MainWindow::createMonitorStreamForSinkInput(uint32_t sink_input_idx, uint32_t sink_idx) {
    pa_stream *s;
    char t[16];
    pa_buffer_attr attr;
    pa_sample_spec ss;
    uint32_t monitor_source_idx;

    ss.channels = 1;
    ss.format = PA_SAMPLE_FLOAT32;
    ss.rate = 25;

    if (!sinkWidgets.count(sink_idx))
        return;

    monitor_source_idx = sinkWidgets[sink_idx]->monitor_index;

    memset(&attr, 0, sizeof(attr));
    attr.fragsize = sizeof(float);
    attr.maxlength = (uint32_t) -1;

    snprintf(t, sizeof(t), "%u", monitor_source_idx);

    if (!(s = pa_stream_new(context, _("Peak detect"), &ss, NULL))) {
        show_error(_("Failed to create monitoring stream"));
        return;
    }

    pa_stream_set_monitor_stream(s, sink_input_idx);
    pa_stream_set_read_callback(s, read_callback, this);
    pa_stream_set_suspended_callback(s, suspended_callback, this);

    if (pa_stream_connect_record(s, t, &attr, (pa_stream_flags_t) (PA_STREAM_DONT_MOVE|PA_STREAM_PEAK_DETECT|PA_STREAM_ADJUST_LATENCY)) < 0) {
        show_error(_("Failed to connect monitoring stream"));
        pa_stream_unref(s);
        return;
    }
}

void MainWindow::updateSource(const pa_source_info &info) {
    SourceWidget *w;
    bool is_new = false;

    if (sourceWidgets.count(info.index))
        w = sourceWidgets[info.index];
    else {
        sourceWidgets[info.index] = w = SourceWidget::create();
        w->setChannelMap(info.channel_map, !!(info.flags & PA_SOURCE_DECIBEL_VOLUME));
        sourcesVBox->pack_start(*w, false, false, 0);
        w->index = info.index;
        is_new = true;

        if (pa_context_get_server_protocol_version(context) >= 13)
            createMonitorStreamForSource(info.index);
    }

    w->updating = true;

    w->name = info.name;
    w->description = info.description;
    w->type = info.monitor_of_sink != PA_INVALID_INDEX ? SOURCE_MONITOR : (info.flags & PA_SOURCE_HARDWARE ? SOURCE_HARDWARE : SOURCE_VIRTUAL);

    w->boldNameLabel->set_text("");
    gchar *txt;
    w->nameLabel->set_markup(txt = g_markup_printf_escaped("%s", info.description));
    g_free(txt);

    w->iconImage->set_from_icon_name("audio-input-microphone", Gtk::ICON_SIZE_SMALL_TOOLBAR);

    w->setVolume(info.volume);
    w->muteToggleButton->set_active(info.mute);

    w->defaultMenuItem.set_active(w->name == defaultSourceName);

    w->updating = false;

    if (is_new)
        updateDeviceVisibility();
}

void MainWindow::setIconFromProplist(Gtk::Image *icon, pa_proplist *l, const char *def) {
    const char *t;

    if ((t = pa_proplist_gets(l, PA_PROP_MEDIA_ICON_NAME)))
        goto finish;

    if ((t = pa_proplist_gets(l, PA_PROP_WINDOW_ICON_NAME)))
        goto finish;

    if ((t = pa_proplist_gets(l, PA_PROP_APPLICATION_ICON_NAME)))
        goto finish;

    if ((t = pa_proplist_gets(l, PA_PROP_MEDIA_ROLE))) {

        if (strcmp(t, "video") == 0 ||
            strcmp(t, "phone") == 0)
            goto finish;

        if (strcmp(t, "music") == 0) {
            t = "audio";
            goto finish;
        }

        if (strcmp(t, "game") == 0) {
            t = "applications-games";
            goto finish;
        }

        if (strcmp(t, "event") == 0) {
            t = "dialog-information";
            goto finish;
        }
    }

    t = def;

finish:

    icon->set_from_icon_name(t, Gtk::ICON_SIZE_SMALL_TOOLBAR);
}

void MainWindow::updateSinkInput(const pa_sink_input_info &info) {
    SinkInputWidget *w;
    bool is_new = false;

    if (sinkInputWidgets.count(info.index))
        w = sinkInputWidgets[info.index];
    else {
        sinkInputWidgets[info.index] = w = SinkInputWidget::create();
        w->setChannelMap(info.channel_map, true);
        streamsVBox->pack_start(*w, false, false, 0);
        w->index = info.index;
        w->clientIndex = info.client;
        w->mainWindow = this;
        is_new = true;

        if (pa_context_get_server_protocol_version(context) >= 13)
            createMonitorStreamForSinkInput(info.index, info.sink);
    }

    w->updating = true;

    w->type = info.client != PA_INVALID_INDEX ? SINK_INPUT_CLIENT : SINK_INPUT_VIRTUAL;

    w->sinkIndex = info.sink;

    char *txt;
    if (clientNames.count(info.client)) {
        w->boldNameLabel->set_markup(txt = g_markup_printf_escaped("<b>%s</b>", clientNames[info.client]));
        g_free(txt);
        w->nameLabel->set_markup(txt = g_markup_printf_escaped(": %s", info.name));
        g_free(txt);
    } else {
        w->boldNameLabel->set_text("");
        w->nameLabel->set_label(info.name);
    }

    setIconFromProplist(w->iconImage, info.proplist, "audio-card");

    w->setVolume(info.volume);
    w->muteToggleButton->set_active(info.mute);

    w->updating = false;

    if (is_new)
        updateDeviceVisibility();
}

void MainWindow::updateSourceOutput(const pa_source_output_info &info) {
    SourceOutputWidget *w;
    const char *app;
    bool is_new = false;

    if ((app = pa_proplist_gets(info.proplist, PA_PROP_APPLICATION_ID)))
        if (strcmp(app, "org.PulseAudio.pavucontrol") == 0)
            return;

    if (sourceOutputWidgets.count(info.index))
        w = sourceOutputWidgets[info.index];
    else {
        sourceOutputWidgets[info.index] = w = SourceOutputWidget::create();
        recsVBox->pack_start(*w, false, false, 0);
        w->index = info.index;
        w->clientIndex = info.client;
        w->mainWindow = this;
    }

    w->updating = true;

    w->type = info.client != PA_INVALID_INDEX ? SOURCE_OUTPUT_CLIENT : SOURCE_OUTPUT_VIRTUAL;

    w->sourceIndex = info.source;

    char *txt;
    if (clientNames.count(info.client)) {
        w->boldNameLabel->set_markup(txt = g_markup_printf_escaped("<b>%s</b>", clientNames[info.client]));
        g_free(txt);
        w->nameLabel->set_markup(txt = g_markup_printf_escaped(": %s", info.name));
        g_free(txt);
    } else {
        w->boldNameLabel->set_text("");
        w->nameLabel->set_label(info.name);
    }

    setIconFromProplist(w->iconImage, info.proplist, "audio-input-microphone");

    w->updating = false;

    if (is_new)
        updateDeviceVisibility();
}

void MainWindow::updateClient(const pa_client_info &info) {

    g_free(clientNames[info.index]);
    clientNames[info.index] = g_strdup(info.name);

    for (std::map<uint32_t, SinkInputWidget*>::iterator i = sinkInputWidgets.begin(); i != sinkInputWidgets.end(); ++i) {
        SinkInputWidget *w = i->second;

        if (!w)
            continue;

        if (w->clientIndex == info.index) {
            gchar *txt;
            w->boldNameLabel->set_markup(txt = g_markup_printf_escaped("<b>%s</b>", info.name));
            g_free(txt);
        }
    }
}

void MainWindow::updateServer(const pa_server_info &info) {

    defaultSourceName = info.default_source_name ? info.default_source_name : "";
    defaultSinkName = info.default_sink_name ? info.default_sink_name : "";

    for (std::map<uint32_t, SinkWidget*>::iterator i = sinkWidgets.begin(); i != sinkWidgets.end(); ++i) {
        SinkWidget *w = i->second;

        if (!w)
            continue;

        w->updating = true;
        w->defaultMenuItem.set_active(w->name == defaultSinkName);
        w->updating = false;
    }

    for (std::map<uint32_t, SourceWidget*>::iterator i = sourceWidgets.begin(); i != sourceWidgets.end(); ++i) {
        SourceWidget *w = i->second;

        if (!w)
            continue;

        w->updating = true;
        w->defaultMenuItem.set_active(w->name == defaultSourceName);
        w->updating = false;
    }
}

bool MainWindow::createEventRoleWidget() {
    if (eventRoleWidget)
        return FALSE;

    pa_channel_map cm = {
        1, { PA_CHANNEL_POSITION_MONO }
    };

    eventRoleWidget = RoleWidget::create();
    streamsVBox->pack_start(*eventRoleWidget, false, false, 0);
    eventRoleWidget->role = "sink-input-by-media-role:event";
    eventRoleWidget->setChannelMap(cm, true);

    eventRoleWidget->boldNameLabel->set_text("");
    eventRoleWidget->nameLabel->set_label(_("System Sounds"));

    eventRoleWidget->iconImage->set_from_icon_name("multimedia-volume-control", Gtk::ICON_SIZE_SMALL_TOOLBAR);

    eventRoleWidget->device = "";

    eventRoleWidget->updating = true;

    pa_cvolume volume;
    volume.channels = 1;
    volume.values[0] = PA_VOLUME_NORM;

    eventRoleWidget->setVolume(volume);
    eventRoleWidget->muteToggleButton->set_active(false);

    eventRoleWidget->updating = false;

    return TRUE;
}

void MainWindow::deleteEventRoleWidget() {

    if (eventRoleWidget)
        delete eventRoleWidget;

    eventRoleWidget = NULL;
}

void MainWindow::updateRole(const pa_ext_stream_restore_info &info) {
    pa_cvolume volume;
    bool is_new = false;

    if (strcmp(info.name, "sink-input-by-media-role:event") != 0)
        return;

    is_new = createEventRoleWidget();

    eventRoleWidget->updating = true;

    eventRoleWidget->device = info.device ? info.device : "";

    volume.channels = 1;
    volume.values[0] = pa_cvolume_max(&info.volume);

    eventRoleWidget->setVolume(volume);
    eventRoleWidget->muteToggleButton->set_active(info.mute);

    eventRoleWidget->updating = false;

    if (is_new)
        updateDeviceVisibility();
}

void MainWindow::updateVolumeMeter(uint32_t source_index, uint32_t sink_input_idx, double v) {

    if (sink_input_idx != PA_INVALID_INDEX) {
        SinkInputWidget *w;

        if (sinkInputWidgets.count(sink_input_idx)) {
            w = sinkInputWidgets[sink_input_idx];
            w->updatePeak(v);
        }

    } else {

        for (std::map<uint32_t, SinkWidget*>::iterator i = sinkWidgets.begin(); i != sinkWidgets.end(); ++i) {
            SinkWidget* w = i->second;

            if (w->monitor_index == source_index)
                w->updatePeak(v);
        }

        for (std::map<uint32_t, SourceWidget*>::iterator i = sourceWidgets.begin(); i != sourceWidgets.end(); ++i) {
            SourceWidget* w = i->second;

            if (w->index == source_index)
                w->updatePeak(v);
        }

        for (std::map<uint32_t, SourceOutputWidget*>::iterator i = sourceOutputWidgets.begin(); i != sourceOutputWidgets.end(); ++i) {
            SourceOutputWidget* w = i->second;

            if (w->sourceIndex == source_index)
                w->updatePeak(v);
        }
    }
}

static guint idle_source = 0;

gboolean idle_cb(gpointer data) {
    ((MainWindow*) data)->reallyUpdateDeviceVisibility();
    idle_source = 0;
    return FALSE;
}

void MainWindow::updateDeviceVisibility() {

    if (idle_source)
        return;

    idle_source = g_idle_add(idle_cb, this);
}

void MainWindow::reallyUpdateDeviceVisibility() {
    bool is_empty = true;

    for (std::map<uint32_t, SinkInputWidget*>::iterator i = sinkInputWidgets.begin(); i != sinkInputWidgets.end(); ++i) {
        SinkInputWidget* w = i->second;

        if (showSinkInputType == SINK_INPUT_ALL || w->type == showSinkInputType) {
            w->show();
            is_empty = false;
        } else
            w->hide();
    }

    if (eventRoleWidget)
        is_empty = false;

    if (is_empty)
        noStreamsLabel->show();
    else
        noStreamsLabel->hide();

    is_empty = true;

    for (std::map<uint32_t, SourceOutputWidget*>::iterator i = sourceOutputWidgets.begin(); i != sourceOutputWidgets.end(); ++i) {
        SourceOutputWidget* w = i->second;

        if (showSourceOutputType == SOURCE_OUTPUT_ALL || w->type == showSourceOutputType) {
            w->show();
            is_empty = false;
        } else
            w->hide();
    }

    if (is_empty)
        noRecsLabel->show();
    else
        noRecsLabel->hide();

    is_empty = true;

    for (std::map<uint32_t, SinkWidget*>::iterator i = sinkWidgets.begin(); i != sinkWidgets.end(); ++i) {
        SinkWidget* w = i->second;

        if (showSinkType == SINK_ALL || w->type == showSinkType) {
            w->show();
            is_empty = false;
        } else
            w->hide();
    }

    if (is_empty)
        noSinksLabel->show();
    else
        noSinksLabel->hide();

    is_empty = true;

    for (std::map<uint32_t, SourceWidget*>::iterator i = sourceWidgets.begin(); i != sourceWidgets.end(); ++i) {
        SourceWidget* w = i->second;

        if (showSourceType == SOURCE_ALL ||
            w->type == showSourceType ||
            (showSourceType == SOURCE_NO_MONITOR && w->type != SOURCE_MONITOR)) {
            w->show();
            is_empty = false;
        } else
            w->hide();
    }

    if (is_empty)
        noSourcesLabel->show();
    else
        noSourcesLabel->hide();

    /* Hmm, if I don't call hide()/show() here some widgets will never
     * get their proper space allocated */
    sinksVBox->hide();
    sinksVBox->show();
    sourcesVBox->hide();
    sourcesVBox->show();
    streamsVBox->hide();
    streamsVBox->show();
    recsVBox->hide();
    recsVBox->show();
}

void MainWindow::removeSink(uint32_t index) {
    if (!sinkWidgets.count(index))
        return;

    delete sinkWidgets[index];
    sinkWidgets.erase(index);
    updateDeviceVisibility();
}

void MainWindow::removeSource(uint32_t index) {
    if (!sourceWidgets.count(index))
        return;

    delete sourceWidgets[index];
    sourceWidgets.erase(index);
    updateDeviceVisibility();
}

void MainWindow::removeSinkInput(uint32_t index) {
    if (!sinkInputWidgets.count(index))
        return;

    delete sinkInputWidgets[index];
    sinkInputWidgets.erase(index);
    updateDeviceVisibility();
}

void MainWindow::removeSourceOutput(uint32_t index) {
    if (!sourceOutputWidgets.count(index))
        return;

    delete sourceOutputWidgets[index];
    sourceOutputWidgets.erase(index);
    updateDeviceVisibility();
}

void MainWindow::removeClient(uint32_t index) {
    g_free(clientNames[index]);
    clientNames.erase(index);
}

void MainWindow::onSinkTypeComboBoxChanged() {
    showSinkType = (SinkType) sinkTypeComboBox->get_active_row_number();

    if (showSinkType == (SinkType) -1)
        sinkTypeComboBox->set_active((int) SINK_ALL);

    updateDeviceVisibility();
}

void MainWindow::onSourceTypeComboBoxChanged() {
    showSourceType = (SourceType) sourceTypeComboBox->get_active_row_number();

    if (showSourceType == (SourceType) -1)
        sourceTypeComboBox->set_active((int) SOURCE_NO_MONITOR);

    updateDeviceVisibility();
}

void MainWindow::onSinkInputTypeComboBoxChanged() {
    showSinkInputType = (SinkInputType) sinkInputTypeComboBox->get_active_row_number();

    if (showSinkInputType == (SinkInputType) -1)
        sinkInputTypeComboBox->set_active((int) SINK_INPUT_CLIENT);

    updateDeviceVisibility();
}

void MainWindow::onSourceOutputTypeComboBoxChanged() {
    showSourceOutputType = (SourceOutputType) sourceOutputTypeComboBox->get_active_row_number();

    if (showSourceOutputType == (SourceOutputType) -1)
        sourceOutputTypeComboBox->set_active((int) SOURCE_OUTPUT_CLIENT);

    updateDeviceVisibility();
}

static void dec_outstanding(MainWindow *w) {
    if (n_outstanding <= 0)
        return;

    if (--n_outstanding <= 0)
        w->get_window()->set_cursor();
}

void sink_cb(pa_context *, const pa_sink_info *i, int eol, void *userdata) {
    MainWindow *w = static_cast<MainWindow*>(userdata);

    if (eol < 0) {
        if (pa_context_errno(context) == PA_ERR_NOENTITY)
            return;

        show_error(_("Sink callback failure"));
        return;
    }

    if (eol > 0) {
        dec_outstanding(w);
        return;
    }

    w->updateSink(*i);
}

void source_cb(pa_context *, const pa_source_info *i, int eol, void *userdata) {
    MainWindow *w = static_cast<MainWindow*>(userdata);

    if (eol < 0) {
        if (pa_context_errno(context) == PA_ERR_NOENTITY)
            return;

        show_error(_("Source callback failure"));
        return;
    }

    if (eol > 0) {
        dec_outstanding(w);
        return;
    }

    w->updateSource(*i);
}

void sink_input_cb(pa_context *, const pa_sink_input_info *i, int eol, void *userdata) {
    MainWindow *w = static_cast<MainWindow*>(userdata);

    if (eol < 0) {
        if (pa_context_errno(context) == PA_ERR_NOENTITY)
            return;

        show_error(_("Sink input callback failure"));
        return;
    }

    if (eol > 0) {
        dec_outstanding(w);
        return;
    }

    w->updateSinkInput(*i);
}

void source_output_cb(pa_context *, const pa_source_output_info *i, int eol, void *userdata) {
    MainWindow *w = static_cast<MainWindow*>(userdata);

    if (eol < 0) {
        if (pa_context_errno(context) == PA_ERR_NOENTITY)
            return;

        show_error(_("Source output callback failure"));
        return;
    }

    if (eol > 0)  {

        if (n_outstanding > 0) {
            /* At this point all notebook pages have been populated, so
             * let's open one that isn't empty */

            if (w->sinkInputWidgets.size() > 0)
                w->notebook->set_current_page(0);
            else if (w->sourceOutputWidgets.size() > 0)
                w->notebook->set_current_page(1);
            else if (w->sourceWidgets.size() > 0 && w->sinkWidgets.size() == 0)
                w->notebook->set_current_page(3);
            else
                w->notebook->set_current_page(2);
        }

        dec_outstanding(w);
        return;
    }

    w->updateSourceOutput(*i);
}

void client_cb(pa_context *, const pa_client_info *i, int eol, void *userdata) {
    MainWindow *w = static_cast<MainWindow*>(userdata);

    if (eol < 0) {
        if (pa_context_errno(context) == PA_ERR_NOENTITY)
            return;

        show_error(_("Client callback failure"));
        return;
    }

    if (eol > 0) {
        dec_outstanding(w);
        return;
    }

    w->updateClient(*i);
}

void server_info_cb(pa_context *, const pa_server_info *i, void *userdata) {
    MainWindow *w = static_cast<MainWindow*>(userdata);

    if (!i) {
        show_error(_("Server info callback failure"));
        return;
    }

    w->updateServer(*i);
    dec_outstanding(w);
}

void ext_stream_restore_read_cb(
        pa_context *c,
        const pa_ext_stream_restore_info *i,
        int eol,
        void *userdata) {

    MainWindow *w = static_cast<MainWindow*>(userdata);

    if (eol < 0) {
        g_debug(_("Failed to initialized stream_restore extension: %s"), pa_strerror(pa_context_errno(context)));
        w->deleteEventRoleWidget();
        return;
    }

    w->createEventRoleWidget();

    if (eol > 0) {
        dec_outstanding(w);
        return;
    }

    w->updateRole(*i);
}

static void ext_stream_restore_subscribe_cb(pa_context *c, void *userdata) {
    MainWindow *w = static_cast<MainWindow*>(userdata);
    pa_operation *o;

    if (!(o = pa_ext_stream_restore_read(c, ext_stream_restore_read_cb, w))) {
        show_error(_("pa_ext_stream_restore_read() failed"));
        return;
    }

    pa_operation_unref(o);
}

void subscribe_cb(pa_context *c, pa_subscription_event_type_t t, uint32_t index, void *userdata) {
    MainWindow *w = static_cast<MainWindow*>(userdata);

    switch (t & PA_SUBSCRIPTION_EVENT_FACILITY_MASK) {
        case PA_SUBSCRIPTION_EVENT_SINK:
            if ((t & PA_SUBSCRIPTION_EVENT_TYPE_MASK) == PA_SUBSCRIPTION_EVENT_REMOVE)
                w->removeSink(index);
            else {
                pa_operation *o;
                if (!(o = pa_context_get_sink_info_by_index(c, index, sink_cb, w))) {
                    show_error(_("pa_context_get_sink_info_by_index() failed"));
                    return;
                }
                pa_operation_unref(o);
            }
            break;

        case PA_SUBSCRIPTION_EVENT_SOURCE:
            if ((t & PA_SUBSCRIPTION_EVENT_TYPE_MASK) == PA_SUBSCRIPTION_EVENT_REMOVE)
                w->removeSource(index);
            else {
                pa_operation *o;
                if (!(o = pa_context_get_source_info_by_index(c, index, source_cb, w))) {
                    show_error(_("pa_context_get_source_info_by_index() failed"));
                    return;
                }
                pa_operation_unref(o);
            }
            break;

        case PA_SUBSCRIPTION_EVENT_SINK_INPUT:
            if ((t & PA_SUBSCRIPTION_EVENT_TYPE_MASK) == PA_SUBSCRIPTION_EVENT_REMOVE)
                w->removeSinkInput(index);
            else {
                pa_operation *o;
                if (!(o = pa_context_get_sink_input_info(c, index, sink_input_cb, w))) {
                    show_error(_("pa_context_get_sink_input_info() failed"));
                    return;
                }
                pa_operation_unref(o);
            }
            break;

        case PA_SUBSCRIPTION_EVENT_SOURCE_OUTPUT:
            if ((t & PA_SUBSCRIPTION_EVENT_TYPE_MASK) == PA_SUBSCRIPTION_EVENT_REMOVE)
                w->removeSourceOutput(index);
            else {
                pa_operation *o;
                if (!(o = pa_context_get_source_output_info(c, index, source_output_cb, w))) {
                    show_error(_("pa_context_get_sink_input_info() failed"));
                    return;
                }
                pa_operation_unref(o);
            }
            break;

        case PA_SUBSCRIPTION_EVENT_CLIENT:
            if ((t & PA_SUBSCRIPTION_EVENT_TYPE_MASK) == PA_SUBSCRIPTION_EVENT_REMOVE)
                w->removeClient(index);
            else {
                pa_operation *o;
                if (!(o = pa_context_get_client_info(c, index, client_cb, w))) {
                    show_error(_("pa_context_get_client_info() failed"));
                    return;
                }
                pa_operation_unref(o);
            }
            break;

        case PA_SUBSCRIPTION_EVENT_SERVER: {
            pa_operation *o;
            if (!(o = pa_context_get_server_info(c, server_info_cb, w))) {
                show_error(_("pa_context_get_server_info() failed"));
                return;
            }
            pa_operation_unref(o);
        }
    }
}

void context_state_callback(pa_context *c, void *userdata) {
    MainWindow *w = static_cast<MainWindow*>(userdata);

    g_assert(c);

    switch (pa_context_get_state(c)) {
        case PA_CONTEXT_UNCONNECTED:
        case PA_CONTEXT_CONNECTING:
        case PA_CONTEXT_AUTHORIZING:
        case PA_CONTEXT_SETTING_NAME:
            break;

        case PA_CONTEXT_READY: {
            pa_operation *o;

            pa_context_set_subscribe_callback(c, subscribe_cb, w);

            if (!(o = pa_context_subscribe(c, (pa_subscription_mask_t)
                                           (PA_SUBSCRIPTION_MASK_SINK|
                                            PA_SUBSCRIPTION_MASK_SOURCE|
                                            PA_SUBSCRIPTION_MASK_SINK_INPUT|
                                            PA_SUBSCRIPTION_MASK_SOURCE_OUTPUT|
                                            PA_SUBSCRIPTION_MASK_CLIENT|
                                            PA_SUBSCRIPTION_MASK_SERVER), NULL, NULL))) {
                show_error(_("pa_context_subscribe() failed"));
                return;
            }
            pa_operation_unref(o);

            if (!(o = pa_context_get_server_info(c, server_info_cb, w))) {
                show_error(_("pa_context_get_server_info() failed"));
                return;
            }
            pa_operation_unref(o);

            if (!(o = pa_context_get_client_info_list(c, client_cb, w))) {
                show_error(_("pa_context_client_info_list() failed"));
                return;
            }
            pa_operation_unref(o);

            if (!(o = pa_context_get_sink_info_list(c, sink_cb, w))) {
                show_error(_("pa_context_get_sink_info_list() failed"));
                return;
            }
            pa_operation_unref(o);

            if (!(o = pa_context_get_source_info_list(c, source_cb, w))) {
                show_error(_("pa_context_get_source_info_list() failed"));
                return;
            }
            pa_operation_unref(o);

            if (!(o = pa_context_get_sink_input_info_list(c, sink_input_cb, w))) {
                show_error(_("pa_context_get_sink_input_info_list() failed"));
                return;
            }
            pa_operation_unref(o);

            if (!(o = pa_context_get_source_output_info_list(c, source_output_cb, w))) {
                show_error(_("pa_context_get_source_output_info_list() failed"));
                return;
            }
            pa_operation_unref(o);

            n_outstanding = 6;

            /* This call is not always supported */
            if ((o = pa_ext_stream_restore_read(c, ext_stream_restore_read_cb, w))) {
                pa_operation_unref(o);
                n_outstanding++;

                pa_ext_stream_restore_set_subscribe_cb(c, ext_stream_restore_subscribe_cb, w);

                if ((o = pa_ext_stream_restore_subscribe(c, 1, NULL, NULL)))
                    pa_operation_unref(o);

            } else
                g_debug(_("Failed to initialized stream_restore extension: %s"), pa_strerror(pa_context_errno(context)));

            break;
        }

        case PA_CONTEXT_FAILED:
            show_error(_("Connection failed"));
            return;

        case PA_CONTEXT_TERMINATED:
        default:
            Gtk::Main::quit();
            return;
    }
}

int main(int argc, char *argv[]) {

    /* Initialize the i18n stuff */
    bindtextdomain(GETTEXT_PACKAGE, LOCALEDIR);
    bind_textdomain_codeset(GETTEXT_PACKAGE, "UTF-8");
    textdomain(GETTEXT_PACKAGE);

    signal(SIGPIPE, SIG_IGN);

    Gtk::Main kit(argc, argv);

    ca_context_set_driver(ca_gtk_context_get(), "pulse");

    Gtk::Window* mainWindow = MainWindow::create();

    pa_glib_mainloop *m = pa_glib_mainloop_new(g_main_context_default());
    g_assert(m);
    pa_mainloop_api *api = pa_glib_mainloop_get_api(m);
    g_assert(api);

    pa_proplist *proplist = pa_proplist_new();
    pa_proplist_sets(proplist, PA_PROP_APPLICATION_NAME, _("PulseAudio Volume Control"));
    pa_proplist_sets(proplist, PA_PROP_APPLICATION_ID, "org.PulseAudio.pavucontrol");
    pa_proplist_sets(proplist, PA_PROP_APPLICATION_ICON_NAME, "audio-card");
    pa_proplist_sets(proplist, PA_PROP_APPLICATION_VERSION, PACKAGE_VERSION);

    context = pa_context_new_with_proplist(api, NULL, proplist);
    g_assert(context);

    pa_proplist_free(proplist);

    pa_context_set_state_callback(context, context_state_callback, mainWindow);

    if (pa_context_connect(context, NULL, (pa_context_flags_t) 0, NULL) < 0) {
        show_error(_("Connection failed"));
        goto finish;
    }

    Gtk::Main::run(*mainWindow);
    delete mainWindow;

finish:
    pa_context_unref(context);
    pa_glib_mainloop_free(m);
}
