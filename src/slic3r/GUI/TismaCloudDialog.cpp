///|/ Tisma Slicer: the account of Tisma Cloud and the jobs sent to it from the GUI.
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "TismaCloudDialog.hpp"

#include <atomic>
#include <mutex>
#include <thread>

#include <boost/filesystem.hpp>
#include <boost/log/trivial.hpp>
#include <boost/nowide/convert.hpp>

#include <wx/button.h>
#include <wx/dialog.h>
#include <wx/filedlg.h>
#include <wx/hyperlink.h>
#include <wx/msgdlg.h>
#include <wx/progdlg.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/utils.h>
#if wxUSE_SECRETSTORE
#include <wx/secretstore.h>
#endif

#include "libslic3r/AppConfig.hpp"
#include "libslic3r/Format/3mf.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/MultipleBeds.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/Utils.hpp"
#include "libslic3r/libslic3r.h"
#include "GUI.hpp"
#include "GUI_App.hpp"
#include "I18N.hpp"
#include "MainFrame.hpp"
#include "wxExtensions.hpp"
#include "NotificationManager.hpp"
#include "Plater.hpp"
#include "format.hpp"

namespace Slic3r {
namespace GUI {

namespace {

// The token is kept in the password store of the system when there is one, otherwise in the application settings.
const wxString secret_service() { return wxString::FromUTF8(SLIC3R_APP_NAME) + "/Cloud"; }

bool secret_store_ok()
{
#if wxUSE_SECRETSTORE
    wxString errmsg;
    return wxSecretStore::GetDefault().IsOk(&errmsg);
#else
    return false;
#endif
}

void save_token(const std::string &email, const std::string &token)
{
    AppConfig *config = wxGetApp().app_config;
    config->set("tisma_cloud_email", email);
#if wxUSE_SECRETSTORE
    if (secret_store_ok() &&
        wxSecretStore::GetDefault().Save(secret_service(), wxString::FromUTF8(email), wxSecretValue(wxString::FromUTF8(token)))) {
        config->set("tisma_cloud_token", "");
        return;
    }
#endif
    BOOST_LOG_TRIVIAL(warning) << "No password store: the Tisma Cloud token is kept in the application settings";
    config->set("tisma_cloud_token", token);
}

void clear_token()
{
    AppConfig *config = wxGetApp().app_config;
    config->set("tisma_cloud_token", "");
#if wxUSE_SECRETSTORE
    if (secret_store_ok())
        wxSecretStore::GetDefault().Delete(secret_service());
#endif
}

std::string load_token()
{
    const std::string plain = wxGetApp().app_config->get("tisma_cloud_token");
    if (! plain.empty())
        return plain;
#if wxUSE_SECRETSTORE
    if (secret_store_ok()) {
        wxString user;
        wxSecretValue secret;
        if (wxSecretStore::GetDefault().Load(secret_service(), user, secret))
            return into_u8(secret.GetAsString());
    }
#endif
    return {};
}

wxString hours_text(double hours)
{
    return hours < 1. ? format_wxstr(_L("%1% min"), int(std::round(hours * 60.))) : format_wxstr(_L("%1$.1f h"), hours);
}

class CloudAccountDialog : public wxDialog
{
public:
    explicit CloudAccountDialog(wxWindow *parent)
        : wxDialog(parent, wxID_ANY, _L("Tisma Cloud account"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
    {
        SetFont(wxGetApp().normal_font());
        const int em = wxGetApp().em_unit();
        const CloudAccess access = cloud_access();

        auto *intro = new wxStaticText(this, wxID_ANY, _L("Slice and run the structural analyses on the Tisma Cloud servers. "
                                                          "The account and the plan are managed on the web site."));
        intro->Wrap(40 * em);

        auto *grid = new wxFlexGridSizer(2, em / 2, em);
        grid->AddGrowableCol(1);
        m_url = new wxTextCtrl(this, wxID_ANY, wxString::FromUTF8(access.url), wxDefaultPosition, wxSize(30 * em, -1));
        m_url->SetHint("https://cloud.example.com");
        m_email = new wxTextCtrl(this, wxID_ANY, wxString::FromUTF8(access.email));
        m_password = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_PASSWORD | wxTE_PROCESS_ENTER);
        grid->Add(new wxStaticText(this, wxID_ANY, _L("Server") + ":"), 0, wxALIGN_CENTER_VERTICAL);
        grid->Add(m_url, 1, wxEXPAND);
        grid->Add(new wxStaticText(this, wxID_ANY, _L("E-mail") + ":"), 0, wxALIGN_CENTER_VERTICAL);
        grid->Add(m_email, 1, wxEXPAND);
        grid->Add(new wxStaticText(this, wxID_ANY, _L("Password") + ":"), 0, wxALIGN_CENTER_VERTICAL);
        grid->Add(m_password, 1, wxEXPAND);

        m_state = new wxStaticText(this, wxID_ANY, wxEmptyString);
        // The page of the web site to create an account and pay the plan, given by the server.
        m_billing = new wxHyperlinkCtrl(this, wxID_ANY, _L("Create an account or pay the plan on the web site"), "https://");
        m_billing->Hide();
        m_login  = new wxButton(this, wxID_ANY, _L("Log in"));
        m_logout = new wxButton(this, wxID_ANY, _L("Log out"));
        auto *close = new wxButton(this, wxID_CLOSE, _L("Close"));
        auto *buttons = new wxBoxSizer(wxHORIZONTAL);
        buttons->Add(m_login, 0, wxRIGHT, em / 2);
        buttons->Add(m_logout, 0, wxRIGHT, em / 2);
        buttons->AddStretchSpacer();
        buttons->Add(close);

        auto *top = new wxBoxSizer(wxVERTICAL);
        top->Add(intro, 0, wxALL, em);
        top->Add(grid, 0, wxEXPAND | wxLEFT | wxRIGHT, em);
        top->Add(m_state, 0, wxEXPAND | wxALL, em);
        top->Add(m_billing, 0, wxLEFT | wxRIGHT | wxBOTTOM, em);
        top->Add(buttons, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, em);
        SetSizerAndFit(top);
        SetEscapeId(wxID_CLOSE);

        m_login->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { login(); });
        m_password->Bind(wxEVT_TEXT_ENTER, [this](wxCommandEvent &) { login(); });
        m_logout->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { logout(); });
        close->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { EndModal(wxID_CLOSE); });
        refresh();
        wxGetApp().UpdateDlgDarkUI(this);
        CenterOnParent();
    }

private:
    void refresh()
    {
        const CloudAccess access = cloud_access();
        show_billing(access.url);
        m_logout->Enable(access.logged_in());
        if (! access.logged_in()) {
            set_state(_L("Not logged in."));
            return;
        }
        wxBusyCursor wait;
        TismaCloud::Account account;
        std::string error;
        if (! access.client().account(account, error)) {
            set_state(format_wxstr(_L("Logged in as %1%, but the server does not answer: %2%"), access.email, cloud_error_text(error)));
            return;
        }
        wxString text = format_wxstr(_L("Logged in as %1%. Plan %2%: %3% of %4% used this month."), account.email, account.plan,
                                     hours_text(account.hours_used), hours_text(account.hours_included));
        text += "\n" + (account.paid ? format_wxstr(_L("Paid until %1%."), account.paid_until.substr(0, 10)) :
                                       _L("The plan is not paid: renew it on the web site to use the servers."));
        set_state(text);
    }

    // Only an https page of the web site is offered.
    void show_billing(const std::string &url)
    {
        std::string billing, error;
        if (! url.empty()) {
            wxBusyCursor wait;
            TismaCloud(url, std::string()).health(billing, error);
        }
        const bool show = billing.rfind("https://", 0) == 0;
        if (show)
            m_billing->SetURL(wxString::FromUTF8(billing));
        m_billing->Show(show);
        Layout();
        Fit();
    }

    void set_state(const wxString &text)
    {
        m_state->SetLabel(text);
        m_state->Wrap(40 * wxGetApp().em_unit());
        Layout();
        Fit();
    }

    void login()
    {
        std::string url = into_u8(m_url->GetValue().Strip(wxString::both));
        const std::string email = into_u8(m_email->GetValue().Strip(wxString::both));
        const std::string password = into_u8(m_password->GetValue());
        if (url.empty() || email.empty() || password.empty()) {
            set_state(_L("Enter the server, the e-mail and the password."));
            return;
        }
        if (url.rfind("http://", 0) != 0 && url.rfind("https://", 0) != 0)
            url = "https://" + url;
        wxBusyCursor wait;
        std::string token, error;
        TismaCloud client(url, std::string());
        if (! client.login(email, password, into_u8(wxGetHostName()), token, error)) {
            show_billing(url);
            set_state(format_wxstr(_L("Could not log in: %1%"), cloud_error_text(error)));
            return;
        }
        wxGetApp().app_config->set("tisma_cloud_url", url);
        save_token(email, token);
        m_url->SetValue(wxString::FromUTF8(url));
        m_password->Clear();
        refresh();
    }

    void logout()
    {
        const CloudAccess access = cloud_access();
        if (access.logged_in()) {
            wxBusyCursor wait;
            std::string error;
            access.client().logout(error);
        }
        clear_token();
        refresh();
    }

    wxTextCtrl   *m_url;
    wxTextCtrl   *m_email;
    wxTextCtrl   *m_password;
    wxStaticText *m_state;
    wxHyperlinkCtrl *m_billing;
    wxButton     *m_login;
    wxButton     *m_logout;
};

// Runs a job with a progress dialog that can cancel it. True when the result was downloaded.
bool run_with_progress(wxWindow *parent, const wxString &title, const CloudAccess &access, const std::string &kind,
                       const std::string &params, const std::string &project, const std::string &result,
                       TismaCloud::Job &job, std::string &error)
{
    std::atomic<bool> cancel { false };
    std::atomic<bool> done { false };
    std::mutex        mutex;
    TismaCloud::Job   state;
    bool              ok = false;
    std::thread worker([&]() {
        TismaCloud client = access.client();
        ok = client.run(kind, params, project, result, job, error,
                        [&](const TismaCloud::Job &j) { std::lock_guard<std::mutex> lock(mutex); state = j; }, cancel);
        done = true;
    });
    wxProgressDialog dlg(title, _L("Sending the project to the servers..."), 100, parent,
                         wxPD_APP_MODAL | wxPD_CAN_ABORT | wxPD_AUTO_HIDE | wxPD_ELAPSED_TIME);
    while (! done) {
        TismaCloud::Job j;
        {
            std::lock_guard<std::mutex> lock(mutex);
            j = state;
        }
        wxString message = j.status.empty()   ? _L("Sending the project to the servers...") :
                           j.status == "queued" ? _L("Waiting for a free computing server...") :
                           j.status == "running" ? format_wxstr(_L("Computing on the servers: %1% %%"), j.progress) :
                                                  _L("Downloading the result...");
        if (! dlg.Update(std::clamp(j.progress, 0, 99), message))
            cancel = true;
        wxMilliSleep(100);
    }
    worker.join();
    return ok;
}

} // namespace

CloudAccess cloud_access()
{
    CloudAccess access;
    const AppConfig *config = wxGetApp().app_config;
    access.url   = config->get("tisma_cloud_url");
    access.email = config->get("tisma_cloud_email");
    if (! access.url.empty())
        access.token = load_token();
    return access;
}

void show_cloud_account_dialog(wxWindow *parent)
{
    CloudAccountDialog dlg(parent);
    dlg.ShowModal();
}

bool ensure_cloud_access(wxWindow *parent, CloudAccess &access)
{
    access = cloud_access();
    if (! access.logged_in()) {
        show_cloud_account_dialog(parent);
        access = cloud_access();
    }
    return access.logged_in();
}

bool export_cloud_project(Model &model, const DynamicPrintConfig &config, const std::string &path, std::string &error)
{
    try {
        if (store_3mf(path.c_str(), &model, &config, false))
            return true;
    } catch (const std::exception &ex) {
        error = ex.what();
        return false;
    }
    error = _u8L("The project could not be saved for the servers.");
    return false;
}

std::string cloud_temp_path(const std::string &extension)
{
    namespace fs = boost::filesystem;
    return (fs::temp_directory_path() / fs::unique_path("tisma-cloud-%%%%-%%%%-%%%%" + extension)).string();
}

wxString cloud_error_text(const std::string &error)
{
    if (error == "Invalid access token")
        return _L("The session ended: log in again (Configuration > Tisma Cloud account).");
    if (error == "Cancelled")
        return _L("Cancelled.");
    if (error.find("not verified") != std::string::npos)
        return _L("Confirm your e-mail first with the code we sent to it (the link below opens the account page).");
    if (error.find("not paid") != std::string::npos)
        return _L("The plan is not paid: pay it on the web site (Configuration > Tisma Cloud account has the link).");
    return wxString::FromUTF8(error);
}

void slice_in_cloud(Plater *plater)
{
    if (plater == nullptr)
        return;
    wxWindow *parent = wxGetApp().mainframe;
    if (plater->printer_technology() != ptFFF) {
        show_error(parent, _L("Slicing in the cloud is available for filament printers. Resin printers are sliced on this computer."));
        return;
    }
    if (plater->model().objects.empty()) {
        show_error(parent, _L("There is nothing to slice."));
        return;
    }
    CloudAccess access;
    if (! ensure_cloud_access(parent, access))
        return;

    // The active plate as the first one of a project: the servers slice the first plate.
    const std::string project = cloud_temp_path(".3mf");
    std::string error;
    bool exported = false;
    {
        Model model(plater->model());
        const int bed = s_multiple_beds.get_active_bed();
        MultipleBedsUtils::with_single_bed_model_fff(model, bed, [&]() {
            Model plate;
            for (const ModelObject *mo : model.objects)
                if (! mo->instances.empty())
                    plate.add_object(*mo);
            plate.plates[0] = model.plates[size_t(bed)];
            exported = export_cloud_project(plate, wxGetApp().preset_bundle->full_config(), project, error);
        });
    }
    if (! exported) {
        show_error(parent, error);
        return;
    }
    const std::string result = cloud_temp_path(".gcode");
    TismaCloud::Job job;
    const bool ok = run_with_progress(parent, _L("Slice in the cloud"), access, "slice", "{}", project, result, job, error);
    boost::system::error_code ec;
    boost::filesystem::remove(project, ec);
    if (! ok) {
        boost::filesystem::remove(result, ec);
        if (error != "Cancelled")
            show_error(parent, format_wxstr(_L("Slicing in the cloud failed: %1%"), cloud_error_text(error)));
        return;
    }
    const std::string ext = boost::filesystem::path(job.result_filename).extension().string();
    const wxString ext_w = wxString::FromUTF8(ext.empty() ? std::string(".gcode") : ext);
    wxFileDialog dlg(parent, _L("Save the G-code as:"), wxEmptyString, plater->get_project_filename(ext_w),
                     "G-code (*" + ext_w + ")|*" + ext_w, wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    if (dlg.ShowModal() == wxID_OK) {
        const std::string target = into_u8(dlg.GetPath());
        std::string copy_error;
        if (copy_file(result, target, copy_error, false) != SUCCESS)
            show_error(parent, format_wxstr(_L("Could not save %1%: %2%"), target, copy_error));
        else {
            wxString note = format_wxstr(_L("Sliced in the cloud in %1$.1f s: %2%."), job.seconds, target);
            if (auto it = job.summary.find("print_time"); it != job.summary.end())
                note += " " + format_wxstr(_L("Print time %1%."), it->second);
            if (NotificationManager *nm = plater->get_notification_manager())
                nm->push_notification(into_u8(note));
        }
    }
    boost::filesystem::remove(result, ec);
}

} // namespace GUI
} // namespace Slic3r
