///|/ Tisma Slicer: the account of Tisma Cloud (server, log in) and the jobs sent to it from the GUI.
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_GUI_TismaCloudDialog_hpp_
#define slic3r_GUI_TismaCloudDialog_hpp_

#include <string>

#include <wx/string.h>

#include "slic3r/Utils/TismaCloud.hpp"

class wxWindow;

namespace Slic3r {

class Model;
class DynamicPrintConfig;

namespace GUI {

class Plater;

// The server and the token of this device (empty when not logged in).
struct CloudAccess
{
    std::string url;
    std::string email;
    std::string token;
    bool logged_in() const { return ! url.empty() && ! token.empty(); }
    TismaCloud client() const { return TismaCloud(url, token); }
};

CloudAccess cloud_access();

// The dialog of the account: server address, log in with e-mail and password, log out, plan and hours left.
void show_cloud_account_dialog(wxWindow *parent);

// The access, opening the account dialog first when not logged in. False when the user did not log in.
bool ensure_cloud_access(wxWindow *parent, CloudAccess &access);

// Saves the model with the print configuration as a project for a computing server.
bool export_cloud_project(Model &model, const DynamicPrintConfig &config, const std::string &path, std::string &error);

// A temporary file name for a job (in the temporary directory).
std::string cloud_temp_path(const std::string &extension);

// Text of an error of a job for the user: the session ended (log in again), plan not paid, ...
wxString cloud_error_text(const std::string &error);

// Slices the active plate on the servers and saves the G-code where the user chooses.
void slice_in_cloud(Plater *plater);

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GUI_TismaCloudDialog_hpp_
