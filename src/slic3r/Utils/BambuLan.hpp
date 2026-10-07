///|/ Copyright (c) Tisma Slicer contributors
///|/
///|/ Tisma Slicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_BambuLan_hpp_
#define slic3r_BambuLan_hpp_

#include "BambuLanClient.hpp"
#include "PrintHost.hpp"

namespace Slic3r {

class DynamicPrintConfig;

// Print host for Bambu Lab printers in LAN only + Developer mode (see BambuLanClient).
class BambuLan : public PrintHost
{
public:
    explicit BambuLan(DynamicPrintConfig *config);
    ~BambuLan() override = default;

    const char *get_name() const override { return "Bambu Lab (LAN)"; }
    bool        test(wxString &msg) const override;
    wxString    get_test_ok_msg() const override;
    wxString    get_test_failed_msg(wxString &msg) const override;
    bool        upload(PrintHostUpload upload_data, ProgressFn progress_fn, ErrorFn error_fn, InfoFn info_fn) const override;
    bool        has_auto_discovery() const override { return false; }
    bool        can_test() const override { return true; }
    PrintHostPostUploadActions get_post_upload_actions() const override { return PrintHostPostUploadAction::StartPrint; }
    std::string get_host() const override { return m_client.host; }

    const BambuLanClient &client() const { return m_client; }

private:
    BambuLanClient m_client;
    bool           m_use_ams{ false };
};

} // namespace Slic3r

#endif // slic3r_BambuLan_hpp_
