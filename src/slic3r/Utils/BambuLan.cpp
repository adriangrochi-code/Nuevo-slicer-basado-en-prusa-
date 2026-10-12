///|/ Copyright (c) Tisma Slicer contributors
///|/
///|/ Tisma Slicer is released under the terms of the AGPLv3 or higher
///|/
#include "BambuLan.hpp"

#include <cstring>

#include <boost/algorithm/string.hpp>
#include <boost/filesystem.hpp>

#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Utils.hpp"
#include "slic3r/GUI/GUI.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/format.hpp"

namespace fs = boost::filesystem;

namespace Slic3r {

BambuLan::BambuLan(DynamicPrintConfig *config)
{
    m_client.host        = config->opt_string("print_host");
    m_client.access_code = config->opt_string("printhost_apikey");
    m_client.cafile      = config->opt_string("printhost_cafile");
    // Only the address: no scheme, port or path.
    std::string &host = m_client.host;
    for (const char *scheme : { "http://", "https://", "mqtt://", "ftps://" })
        if (host.rfind(scheme, 0) == 0)
            host.erase(0, strlen(scheme));
    if (const size_t end = host.find_first_of(":/"); end != std::string::npos)
        host.erase(end);
    if (m_client.cafile.empty())
        m_client.cafile = (fs::path(resources_dir()) / "cert" / "bambu_printer_ca.pem").string();
    if (const ConfigOption *opt = config->option("bambu_use_ams"); opt != nullptr)
        m_use_ams = opt->getBool();
}

bool BambuLan::test(wxString &msg) const
{
    BambuStatus status;
    std::string error;
    if (!m_client.query_status(status, error)) {
        msg = error == "access code" ? _L("The printer rejected the access code.") : GUI::from_u8(error);
        return false;
    }
    msg = GUI::format_wxstr(_L("Printer %1%, state %2%."), status.serial, status.gcode_state);
    return true;
}

wxString BambuLan::get_test_ok_msg() const
{
    return _L("Connection to the Bambu Lab printer works correctly.");
}

wxString BambuLan::get_test_failed_msg(wxString &msg) const
{
    return GUI::format_wxstr("%s: %s\n\n%s", _L("Could not connect to the Bambu Lab printer"), msg,
                             _L("The printer must be in LAN only mode with Developer mode on, and the access code is the one "
                                "shown on its screen (Settings > LAN only)."));
}

bool BambuLan::upload(PrintHostUpload upload_data, ProgressFn progress_fn, ErrorFn error_fn, InfoFn /*info_fn*/) const
{
    const std::string ext = boost::algorithm::to_lower_copy(upload_data.source_path.extension().string());
    if (ext == ".bgcode" || ext == ".bgc") {
        error_fn(_L("Bambu Lab printers need text G-code: turn off binary G-code for this printer."));
        return false;
    }
    std::string error;
    const bool start = upload_data.post_action == PrintHostPostUploadAction::StartPrint;
    const bool ok = m_client.print(upload_data.source_path, upload_data.upload_path.filename().string(), start, m_use_ams,
        [&progress_fn](size_t sent, size_t total) {
            bool cancel = false;
            if (progress_fn) {
                static const std::string empty;
                progress_fn(Http::Progress(0, 0, total, sent, empty), cancel);
            }
            return !cancel;
        }, error);
    if (ok)
        return true;
    if (error == "cancelled")
        return false;
    if (error == "access code")
        error_fn(_L("The printer rejected the access code."));
    else if (error.rfind("start: ", 0) == 0)
        error_fn(GUI::format_wxstr(_L("The file was uploaded but the print did not start: %1%"), error.substr(7)));
    else
        error_fn(GUI::format_wxstr(_L("Upload to the printer failed: %1%"), error));
    return false;
}

} // namespace Slic3r
