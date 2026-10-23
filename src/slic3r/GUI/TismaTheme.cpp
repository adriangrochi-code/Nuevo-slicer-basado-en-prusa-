///|/ Tisma Slicer: appearance of the interface (light, dark or the one of the system).
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "TismaTheme.hpp"

#include <cstdlib>
#include <string>

#include <boost/algorithm/string/trim.hpp>
#include <boost/nowide/fstream.hpp>

#include <libslic3r/AppConfig.hpp>

#include "GUI_App.hpp"
#include "GUI_Utils.hpp"

#ifdef _WIN32
#include <wx/msw/registry.h>
#endif
#ifdef __WXGTK__
#include <gtk/gtk.h>
#endif
#ifdef __APPLE__
#include "slic3r/Utils/MacDarkMode.hpp"
#include <wx/platinfo.h>
#endif

namespace Slic3r {
namespace GUI {
namespace TismaTheme {

Appearance appearance()
{
    const std::string value = wxGetApp().app_config ? wxGetApp().app_config->get("appearance") : std::string();
    return value == "light" ? Appearance::Light : value == "dark" ? Appearance::Dark : Appearance::System;
}

std::string appearance_key(Appearance appearance)
{
    return appearance == Appearance::Light ? "light" : appearance == Appearance::Dark ? "dark" : "system";
}

bool system_dark()
{
#ifdef _WIN32
    // The mode of the apps of Windows 10 / 11 (Settings > Personalization > Colors).
    wxRegKey key(wxRegKey::HKCU, "Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize");
    long light = 1;
    if (key.Exists() && key.HasValue("AppsUseLightTheme") && key.QueryValue("AppsUseLightTheme", &light))
        return light == 0;
    return false;
#elif defined(__APPLE__)
    // The dark appearance was introduced in macOS 10.14.
    return wxPlatformInfo::Get().CheckOSVersion(10, 14) && mac_dark_mode();
#else
    return check_dark_mode();
#endif
}

bool dark()
{
    switch (appearance()) {
    case Appearance::Light: return false;
    case Appearance::Dark:  return true;
    default:                return system_dark();
    }
}

void apply_to_toolkit()
{
    const Appearance mode = appearance();
#ifdef _WIN32
    // The dark mode of PrusaSlicer for Windows (NppDarkMode) reads dark_color_mode.
    wxGetApp().app_config->set("dark_color_mode", dark() ? "1" : "0");
#elif defined(__WXGTK__)
    // A forced mode asks the GTK theme for its dark or light variant (Adwaita and most themes have both).
    // The theme was chosen before GTK started (set_gtk_theme_env()); the dark variant is also asked of the theme.
    if (mode != Appearance::System)
        if (GtkSettings *settings = gtk_settings_get_default())
            g_object_set(settings, "gtk-application-prefer-dark-theme", mode == Appearance::Dark ? TRUE : FALSE, nullptr);
    // Compact text entries: the fields of the settings (TextInput) are as high as their entry, which the GTK themes
    // pad for touch screens. The selected rows of the lists (objects) in the accent color.
    static bool css_added = false;
    if (! css_added) {
        if (GdkScreen *screen = gdk_screen_get_default()) {
            GtkCssProvider *css = gtk_css_provider_new();
            gtk_css_provider_load_from_data(css,
                "entry { min-height: 0; padding-top: 2px; padding-bottom: 2px; }"
                "treeview.view:selected, treeview.view:selected:focus { background-color: #7A24C9; color: #FFFFFF; }",
                -1, nullptr);
            gtk_style_context_add_provider_for_screen(screen, GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
            g_object_unref(css);
            css_added = true;
        }
    }
#elif defined(__APPLE__)
    mac_set_appearance(int(mode));
#else
    (void)mode;
#endif
}

void set_gtk_theme_env(const std::string &ini_path)
{
#ifdef __WXGTK__
    if (const char *env = std::getenv("GTK_THEME"); env != nullptr && *env != 0)
        return;
    boost::nowide::ifstream file(ini_path);
    std::string line;
    while (std::getline(file, line)) {
        // Only the lines before the first [section]: the general settings.
        if (! line.empty() && line.front() == '[')
            break;
        std::string key, value;
        if (const size_t eq = line.find('='); eq != std::string::npos) {
            key   = boost::algorithm::trim_copy(line.substr(0, eq));
            value = boost::algorithm::trim_copy(line.substr(eq + 1));
        }
        if (key == "appearance") {
            if (value == "dark")
                setenv("GTK_THEME", "Adwaita:dark", 1);
            else if (value == "light")
                setenv("GTK_THEME", "Adwaita", 1);
            break;
        }
    }
#else
    (void)ini_path;
#endif
}

} // namespace TismaTheme
} // namespace GUI
} // namespace Slic3r
