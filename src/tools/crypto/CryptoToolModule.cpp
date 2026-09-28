#include "tools/crypto/CryptoToolModule.h"
#include "tools/crypto/CryptoEngine.h"

#include <algorithm>
#include <string>
#include <vector>

#include <wx/button.h>
#include <wx/choice.h>
#include <wx/clipbrd.h>
#include <wx/dataobj.h>
#include <wx/filedlg.h>
#include <wx/ffile.h>
#include <wx/msgdlg.h>
#include <wx/panel.h>
#include <wx/radiobut.h>
#include <wx/sizer.h>
#include <wx/statbox.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

#include <base64.h>
#include <filters.h>
#include <hex.h>

#include "core/Localization.h"

namespace
{
enum DataFormat { Utf8Format, HexFormat, Base64Format, FileFormat };

std::string Utf8(const wxString& value)
{
    const wxCharBuffer bytes = value.utf8_str();
    return std::string(bytes.data(), bytes.length());
}

wxString FromUtf8(const std::string& bytes)
{
    if (bytes.empty()) return wxEmptyString;
    size_t length = 0;
    wxWCharBuffer converted = wxConvUTF8.cMB2WC(bytes.data(), bytes.size(), &length);
    if (!converted) throw CryptoPP::InvalidArgument("Result is not valid UTF-8; select Hex, Base64 or file output");
    return wxString(converted.data(), length);
}

std::string DecodeText(const wxString& text, DataFormat format)
{
    if (format == Utf8Format) return Utf8(text);
    std::string encoded = Utf8(text), decoded;
    if (format == HexFormat)
        CryptoPP::StringSource(encoded, true, new CryptoPP::HexDecoder(new CryptoPP::StringSink(decoded)));
    else
        CryptoPP::StringSource(encoded, true, new CryptoPP::Base64Decoder(new CryptoPP::StringSink(decoded)));
    return decoded;
}

wxString EncodeText(const std::string& bytes, DataFormat format)
{
    if (format == Utf8Format) return FromUtf8(bytes);
    std::string encoded;
    if (format == HexFormat)
        CryptoPP::StringSource(bytes, true, new CryptoPP::HexEncoder(new CryptoPP::StringSink(encoded), true));
    else
        CryptoPP::StringSource(bytes, true, new CryptoPP::Base64Encoder(new CryptoPP::StringSink(encoded), false));
    return wxString::FromUTF8(encoded.c_str());
}

std::string ReadFile(const wxString& path)
{
    wxFFile file(path, wxS("rb"));
    if (!file.IsOpened() || file.Length() < 0) throw CryptoPP::InvalidArgument("Unable to open input file");
    std::string bytes(static_cast<size_t>(file.Length()), '\0');
    if (!bytes.empty() && file.Read(&bytes[0], bytes.size()) != bytes.size())
        throw CryptoPP::InvalidArgument("Unable to read input file");
    return bytes;
}

void WriteFile(const wxString& path, const std::string& bytes)
{
    wxFFile file(path, wxS("wb"));
    if (!file.IsOpened() || (!bytes.empty() && file.Write(bytes.data(), bytes.size()) != bytes.size()) || !file.Close())
        throw CryptoPP::InvalidArgument("Unable to write output file");
}

wxString Pem(const std::string& der, bool privateKey)
{
    std::string body;
    CryptoPP::StringSource(der, true,
        new CryptoPP::Base64Encoder(new CryptoPP::StringSink(body), true, 64));
    const char* name = privateKey ? "RSA PRIVATE KEY" : "RSA PUBLIC KEY";
    return wxString::Format(wxS("-----BEGIN %s-----\n%s-----END %s-----"),
        wxString::FromUTF8(name), wxString::FromUTF8(body.c_str()), wxString::FromUTF8(name));
}

std::string DecodeKey(const wxString& value)
{
    wxString clean = value;
    if (clean.Find(wxS("-----BEGIN")) != wxNOT_FOUND)
    {
        wxArrayString lines = wxSplit(clean, '\n');
        clean.clear();
        for (size_t i = 0; i < lines.size(); ++i)
            if (!lines[i].StartsWith(wxS("-----"))) clean += lines[i].Trim(true).Trim(false);
    }
    return DecodeText(clean, Base64Format);
}

class RsaKeysDialog : public wxDialog
{
public:
    RsaKeysDialog(wxWindow* parent, const wxString& publicKey, const wxString& privateKey)
        : wxDialog(parent, wxID_ANY, ITOOL_TR("RSA key pair"), wxDefaultPosition, wxSize(720, 600),
                   wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
    {
        wxBoxSizer* root = new wxBoxSizer(wxVERTICAL);
        root->Add(new wxStaticText(this, wxID_ANY, ITOOL_TR("Public key (PKCS#1 PEM)")), 0, wxLEFT | wxRIGHT | wxTOP, 8);
        m_public = new wxTextCtrl(this, wxID_ANY, publicKey, wxDefaultPosition, wxDefaultSize,
                                  wxTE_MULTILINE | wxTE_READONLY | wxTE_RICH2);
        root->Add(m_public, 1, wxEXPAND | wxALL, 8);
        root->Add(new wxStaticText(this, wxID_ANY, ITOOL_TR("Private key (PKCS#1 PEM; keep it secure)")), 0, wxLEFT | wxRIGHT, 8);
        m_private = new wxTextCtrl(this, wxID_ANY, privateKey, wxDefaultPosition, wxDefaultSize,
                                   wxTE_MULTILINE | wxTE_READONLY | wxTE_RICH2);
        root->Add(m_private, 1, wxEXPAND | wxALL, 8);
        wxBoxSizer* buttons = new wxBoxSizer(wxHORIZONTAL);
        wxButton* usePublic = new wxButton(this, wxID_APPLY, ITOOL_TR("Use public key"));
        wxButton* usePrivate = new wxButton(this, wxID_YES, ITOOL_TR("Use private key"));
        buttons->Add(usePublic, 0, wxRIGHT, 8); buttons->Add(usePrivate, 0, wxRIGHT, 8);
        buttons->AddStretchSpacer(); buttons->Add(new wxButton(this, wxID_CLOSE, ITOOL_TR("Close")));
        root->Add(buttons, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);
        SetSizer(root);
        usePublic->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { EndModal(wxID_APPLY); });
        usePrivate->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { EndModal(wxID_YES); });
        Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { EndModal(wxID_CLOSE); }, wxID_CLOSE);
    }
private:
    wxTextCtrl *m_public, *m_private;
};

class CryptoPanel : public wxPanel
{
public:
    explicit CryptoPanel(wxWindow* parent) : wxPanel(parent)
    {
        Build();
        UpdateAlgorithm();
        UpdateDataModes();
    }

private:
    void Build()
    {
        wxBoxSizer* root = new wxBoxSizer(wxVERTICAL);
        wxStaticBoxSizer* settings = new wxStaticBoxSizer(wxVERTICAL, this, ITOOL_TR("Algorithm and parameters"));
        wxBoxSizer* top = new wxBoxSizer(wxHORIZONTAL);
        top->Add(new wxStaticText(this, wxID_ANY, ITOOL_TR("Algorithm:")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
        m_algorithm = new wxChoice(this, wxID_ANY);
        const wxString algorithms[] = {
            wxS("AES"), wxS("SM4"), ITOOL_TR("DES (legacy)"), ITOOL_TR("3DES (legacy)"),
            wxS("Blowfish"), wxS("Twofish"), wxS("CAST5"), wxS("IDEA"), wxS("Serpent"),
            wxS("TEA"), wxS("XTEA"), ITOOL_TR("XXTEA / BTEA (experimental)"), ITOOL_TR("RC4 (legacy)"),
            wxS("RC5"), wxS("RC6"), wxS("ChaCha20-IETF"), wxS("Salsa20"),
            wxS("Camellia"), wxS("SEED"), wxS("RSA")
        };
        for (size_t i = 0; i < sizeof(algorithms) / sizeof(algorithms[0]); ++i) m_algorithm->Append(algorithms[i]);
        m_algorithm->SetSelection(0);
        top->Add(m_algorithm, 0, wxRIGHT, 12);
        top->Add(new wxStaticText(this, wxID_ANY, ITOOL_TR("Mode:")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
        m_mode = new wxChoice(this, wxID_ANY); top->Add(m_mode, 0, wxRIGHT, 12);
        m_encrypt = new wxRadioButton(this, wxID_ANY, ITOOL_TR("Encrypt"), wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
        m_decrypt = new wxRadioButton(this, wxID_ANY, ITOOL_TR("Decrypt")); m_encrypt->SetValue(true);
        top->Add(m_encrypt, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10);
        top->Add(m_decrypt, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 16);
        m_aesSizeLabel = new wxStaticText(this, wxID_ANY, ITOOL_TR("Key length:")); top->Add(m_aesSizeLabel, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
        m_aesSize = new wxChoice(this, wxID_ANY);
        top->Add(m_aesSize, 0);
        settings->Add(top, 0, wxEXPAND | wxALL, 6);

        wxBoxSizer* paddingRow = new wxBoxSizer(wxHORIZONTAL);
        m_paddingLabel = new wxStaticText(this, wxID_ANY, ITOOL_TR("Padding:"));
        paddingRow->Add(m_paddingLabel, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
        m_padding = new wxChoice(this, wxID_ANY);
        m_padding->Append(ITOOL_TR("PKCS#7 (recommended)"));
        m_padding->Append(wxS("Zero Padding"));
        m_padding->Append(wxS("ISO/IEC 7816-4"));
        m_padding->Append(wxS("No Padding"));
        m_padding->SetSelection(0);
        paddingRow->Add(m_padding, 0, wxRIGHT, 10);
        m_paddingHint = new wxStaticText(this, wxID_ANY, ITOOL_TR("Padding is added automatically and removed during decryption."));
        paddingRow->Add(m_paddingHint, 1, wxALIGN_CENTER_VERTICAL);
        settings->Add(paddingRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);

        wxBoxSizer* keyRow = new wxBoxSizer(wxHORIZONTAL);
        m_keyLabel = new wxStaticText(this, wxID_ANY, ITOOL_TR("Key (Hex):")); keyRow->Add(m_keyLabel, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
        m_key = new wxTextCtrl(this, wxID_ANY); keyRow->Add(m_key, 1, wxRIGHT, 5);
        m_generateKey = new wxButton(this, wxID_ANY, ITOOL_TR("Generate random")); keyRow->Add(m_generateKey, 0, wxRIGHT, 5);
        m_rsaBits = new wxChoice(this, wxID_ANY); m_rsaBits->Append(wxS("2048 bit")); m_rsaBits->Append(wxS("3072 bit")); m_rsaBits->Append(wxS("4096 bit")); m_rsaBits->SetSelection(0);
        keyRow->Add(m_rsaBits, 0);
        settings->Add(keyRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);

        wxBoxSizer* ivRow = new wxBoxSizer(wxHORIZONTAL);
        m_ivLabel = new wxStaticText(this, wxID_ANY, ITOOL_TR("IV / Nonce (Hex):")); ivRow->Add(m_ivLabel, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
        m_iv = new wxTextCtrl(this, wxID_ANY); ivRow->Add(m_iv, 1, wxRIGHT, 5);
        m_generateIv = new wxButton(this, wxID_ANY, ITOOL_TR("Generate random")); ivRow->Add(m_generateIv, 0, wxRIGHT, 12);
        m_tagLabel = new wxStaticText(this, wxID_ANY, ITOOL_TR("Tag:")); ivRow->Add(m_tagLabel, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
        m_tagSize = new wxChoice(this, wxID_ANY); m_tagSize->Append(wxS("12 bytes")); m_tagSize->Append(wxS("16 bytes")); m_tagSize->SetSelection(1); ivRow->Add(m_tagSize, 0);
        settings->Add(ivRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);
        wxBoxSizer* aadRow = new wxBoxSizer(wxHORIZONTAL);
        m_aadLabel = new wxStaticText(this, wxID_ANY, ITOOL_TR("AAD (UTF-8):")); aadRow->Add(m_aadLabel, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
        m_aad = new wxTextCtrl(this, wxID_ANY); aadRow->Add(m_aad, 1);
        settings->Add(aadRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);
        m_warning = new wxStaticText(this, wxID_ANY, wxEmptyString); settings->Add(m_warning, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);
        root->Add(settings, 0, wxEXPAND | wxALL, 8);

        wxBoxSizer* panes = new wxBoxSizer(wxHORIZONTAL);
        panes->Add(BuildDataBox(ITOOL_TR("Input"), true), 1, wxEXPAND | wxRIGHT, 4);
        panes->Add(BuildDataBox(ITOOL_TR("Output"), false), 1, wxEXPAND | wxLEFT, 4);
        root->Add(panes, 1, wxEXPAND | wxLEFT | wxRIGHT, 8);

        wxBoxSizer* actions = new wxBoxSizer(wxHORIZONTAL);
        m_run = new wxButton(this, wxID_ANY, ITOOL_TR("Encrypt"), wxDefaultPosition, wxSize(100, -1));
        wxButton* swap = new wxButton(this, wxID_ANY, ITOOL_TR("Swap input and output"));
        wxButton* copy = new wxButton(this, wxID_ANY, ITOOL_TR("Copy result"));
        wxButton* clear = new wxButton(this, wxID_CLEAR, ITOOL_TR("Clear"));
        actions->Add(m_run, 0, wxRIGHT, 8); actions->Add(swap, 0, wxRIGHT, 8); actions->Add(copy, 0, wxRIGHT, 8); actions->Add(clear, 0, wxRIGHT, 12);
        m_status = new wxStaticText(this, wxID_ANY, ITOOL_TR("Recommended default: AES-256-GCM.")); actions->Add(m_status, 1, wxALIGN_CENTER_VERTICAL);
        root->Add(actions, 0, wxEXPAND | wxALL, 8); SetSizer(root);

        m_algorithm->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) { UpdateAlgorithm(); });
        m_mode->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) { UpdateMode(); });
        m_padding->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) { UpdatePaddingHint(); });
        m_aesSize->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) { UpdateKeyHint(); });
        m_encrypt->Bind(wxEVT_RADIOBUTTON, [this](wxCommandEvent&) { OperationChanged(); });
        m_decrypt->Bind(wxEVT_RADIOBUTTON, [this](wxCommandEvent&) { OperationChanged(); });
        m_generateKey->Bind(wxEVT_BUTTON, &CryptoPanel::GenerateKey, this);
        m_generateIv->Bind(wxEVT_BUTTON, &CryptoPanel::GenerateIv, this);
        m_browseInput->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Browse(true); });
        m_browseOutput->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Browse(false); });
        m_inputFormat->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) { UpdateDataModes(); });
        m_outputFormat->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) { UpdateDataModes(); });
        m_run->Bind(wxEVT_BUTTON, &CryptoPanel::Run, this);
        swap->Bind(wxEVT_BUTTON, &CryptoPanel::Swap, this);
        copy->Bind(wxEVT_BUTTON, &CryptoPanel::Copy, this);
        clear->Bind(wxEVT_BUTTON, &CryptoPanel::Clear, this);
    }

    wxSizer* BuildDataBox(const wxString& title, bool input)
    {
        wxStaticBoxSizer* box = new wxStaticBoxSizer(wxVERTICAL, this, title);
        wxBoxSizer* row = new wxBoxSizer(wxHORIZONTAL);
        row->Add(new wxStaticText(this, wxID_ANY, ITOOL_TR("Format:")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
        wxChoice* format = new wxChoice(this, wxID_ANY);
        format->Append(ITOOL_TR("UTF-8 text")); format->Append(wxS("Hex")); format->Append(wxS("Base64")); format->Append(ITOOL_TR("File"));
        format->SetSelection(input ? 0 : 2); row->Add(format, 0, wxRIGHT, 6);
        wxTextCtrl* path = new wxTextCtrl(this, wxID_ANY); row->Add(path, 1, wxRIGHT, 4);
        wxButton* browse = new wxButton(this, wxID_ANY, wxS("..."), wxDefaultPosition, wxSize(36, -1)); row->Add(browse);
        box->Add(row, 0, wxEXPAND | wxALL, 5);
        wxTextCtrl* text = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
            wxTE_MULTILINE | wxTE_RICH2 | (input ? 0 : wxTE_READONLY));
        box->Add(text, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 5);
        if (input) { m_inputFormat = format; m_inputPath = path; m_browseInput = browse; m_input = text; }
        else { m_outputFormat = format; m_outputPath = path; m_browseOutput = browse; m_output = text; }
        return box;
    }

    crypto_tool::Algorithm Algorithm() const { return static_cast<crypto_tool::Algorithm>(m_algorithm->GetSelection()); }
    crypto_tool::Mode Mode() const { return m_modeValues[static_cast<size_t>(m_mode->GetSelection())]; }
    size_t AesKeySize() const { return m_keySizes.empty() ? 0 : m_keySizes[static_cast<size_t>(m_aesSize->GetSelection())]; }
    unsigned TagSize() const { return m_tagSize->GetSelection() == 0 ? 12U : 16U; }
    crypto_tool::Padding Padding() const { return static_cast<crypto_tool::Padding>(m_padding->GetSelection()); }

    void UpdateAlgorithm()
    {
        const crypto_tool::Algorithm a = Algorithm(); m_mode->Clear(); m_modeValues.clear();
        if (a == crypto_tool::RSA) { AddMode(wxS("OAEP-SHA256"), crypto_tool::OAEP_SHA256); AddMode(wxS("OAEP-SHA1"), crypto_tool::OAEP_SHA1); AddMode(ITOOL_TR("PKCS#1 v1.5 (legacy)"), crypto_tool::PKCS1_V15); }
        else if (a == crypto_tool::RC4 || a == crypto_tool::ChaCha20 || a == crypto_tool::Salsa20) AddMode(ITOOL_TR("Stream cipher"), crypto_tool::Stream);
        else if (a == crypto_tool::XXTEA) AddMode(ITOOL_TR("Raw variable block"), crypto_tool::Raw);
        else if (a == crypto_tool::AES || a == crypto_tool::SM4) { AddMode(ITOOL_TR("GCM (recommended)"), crypto_tool::GCM); AddMode(wxS("CBC"), crypto_tool::CBC); AddMode(wxS("CTR"), crypto_tool::CTR); AddMode(ITOOL_TR("ECB (legacy)"), crypto_tool::ECB); }
        else { AddMode(wxS("CBC"), crypto_tool::CBC); AddMode(wxS("CTR"), crypto_tool::CTR); AddMode(wxS("ECB"), crypto_tool::ECB); }
        m_mode->SetSelection(0);
        ConfigureKeySizes(a);
        const bool variableKey = !m_keySizes.empty(), rsa = a == crypto_tool::RSA;
        m_aesSizeLabel->Show(variableKey); m_aesSize->Show(variableKey); m_rsaBits->Show(rsa);
        m_keyLabel->SetLabel(rsa ? ITOOL_TR("Public key (Base64 DER / PEM):") : ITOOL_TR("Key (Hex):"));
        m_generateKey->SetLabel(rsa ? ITOOL_TR("Generate key pair") : ITOOL_TR("Generate random"));
        UpdateMode(); Layout();
    }

    void AddMode(const wxString& name, crypto_tool::Mode mode) { m_mode->Append(name); m_modeValues.push_back(mode); }

    void ConfigureKeySizes(crypto_tool::Algorithm a)
    {
        m_keySizes.clear(); m_aesSize->Clear();
        if (a == crypto_tool::AES || a == crypto_tool::Twofish || a == crypto_tool::Serpent ||
            a == crypto_tool::RC6 || a == crypto_tool::Camellia)
            { m_keySizes.push_back(16); m_keySizes.push_back(24); m_keySizes.push_back(32); }
        else if (a == crypto_tool::Blowfish)
            { m_keySizes.push_back(8); m_keySizes.push_back(16); m_keySizes.push_back(32); m_keySizes.push_back(56); }
        else if (a == crypto_tool::RC4 || a == crypto_tool::RC5)
            { m_keySizes.push_back(8); m_keySizes.push_back(16); m_keySizes.push_back(24); m_keySizes.push_back(32); }
        else if (a == crypto_tool::ChaCha20 || a == crypto_tool::Salsa20)
            { m_keySizes.push_back(16); m_keySizes.push_back(32); }
        for (size_t i = 0; i < m_keySizes.size(); ++i)
            m_aesSize->Append(wxString::Format(wxS("%u bit"), static_cast<unsigned>(m_keySizes[i] * 8)));
        if (!m_keySizes.empty()) m_aesSize->SetSelection(static_cast<int>(m_keySizes.size() - 1));
    }

    void UpdateMode()
    {
        const crypto_tool::Algorithm a = Algorithm(); const crypto_tool::Mode mode = Mode();
        const bool gcm = mode == crypto_tool::GCM;
        const bool iv = crypto_tool::RequiredIvSize(a, mode) != 0;
        const bool padded = mode == crypto_tool::CBC || mode == crypto_tool::ECB;
        m_paddingLabel->Show(padded); m_padding->Show(padded); m_paddingHint->Show(padded);
        m_ivLabel->Show(iv); m_iv->Show(iv); m_generateIv->Show(iv);
        m_tagLabel->Show(gcm); m_tagSize->Show(gcm); m_aadLabel->Show(gcm); m_aad->Show(gcm);
        if (a == crypto_tool::DES || a == crypto_tool::RC4) m_warning->SetLabel(ITOOL_TR("Warning: This algorithm is insecure and is provided only for legacy compatibility."));
        else if (a == crypto_tool::TripleDES || a == crypto_tool::Blowfish || a == crypto_tool::CAST5 ||
                 a == crypto_tool::IDEA || a == crypto_tool::TEA || a == crypto_tool::XTEA || a == crypto_tool::RC5)
            m_warning->SetLabel(ITOOL_TR("Warning: This algorithm is intended for legacy systems or protocols; prefer AES-GCM for new data."));
        else if (a == crypto_tool::XXTEA) m_warning->SetLabel(ITOOL_TR("Warning: Crypto++ names its XXTEA implementation BTEA and marks it as not thoroughly tested; input must be at least 8 bytes and a multiple of 4."));
        else if (mode == crypto_tool::ECB) m_warning->SetLabel(ITOOL_TR("Warning: ECB exposes data patterns and is not recommended for real data."));
        else if (mode == crypto_tool::PKCS1_V15) m_warning->SetLabel(ITOOL_TR("Warning: Prefer OAEP; PKCS#1 v1.5 is provided only for compatibility."));
        else if (mode == crypto_tool::CBC) m_warning->SetLabel(ITOOL_TR("Note: CBC does not provide integrity authentication; prefer GCM for new applications."));
        else m_warning->SetLabel(wxEmptyString);
        UpdateKeyHint(); UpdatePaddingHint(); Layout();
    }

    void UpdatePaddingHint()
    {
        const bool des = crypto_tool::RequiredBlockSize(Algorithm()) == 8;
        if (m_padding->GetSelection() == 0)
        {
            m_padding->SetString(0, des ? ITOOL_TR("PKCS#5 / PKCS#7 (recommended)") : ITOOL_TR("PKCS#7 (recommended)"));
            m_paddingHint->SetLabel(ITOOL_TR("Padding is added automatically; an extra block is added even when the data is aligned."));
        }
        else if (m_padding->GetSelection() == 1)
            m_paddingHint->SetLabel(ITOOL_TR("Pads with 00; decryption removes trailing 00 bytes and may lose original trailing zero bytes."));
        else if (m_padding->GetSelection() == 2)
            m_paddingHint->SetLabel(ITOOL_TR("Adds 80 first, then pads with 00."));
        else
            m_paddingHint->SetLabel(ITOOL_TR("No padding; input length must be a multiple of the block size."));
    }

    void UpdateKeyHint()
    {
        if (Algorithm() == crypto_tool::RSA) m_key->SetHint(m_encrypt->GetValue() ? ITOOL_TR("Paste an RSA public key") : ITOOL_TR("Paste an RSA private key"));
        else
        {
            const size_t n = crypto_tool::RequiredKeySize(Algorithm(), AesKeySize());
            m_key->SetHint(wxString::Format(ITOOL_TR("Requires %u bytes / %u Hex characters"), static_cast<unsigned>(n), static_cast<unsigned>(n * 2)));
        }
    }

    void OperationChanged()
    {
        m_run->SetLabel(m_encrypt->GetValue() ? ITOOL_TR("Encrypt") : ITOOL_TR("Decrypt"));
        if (Algorithm() == crypto_tool::RSA) m_keyLabel->SetLabel(m_encrypt->GetValue() ? ITOOL_TR("Public key (Base64 DER / PEM):") : ITOOL_TR("Private key (Base64 DER / PEM):"));
        UpdateKeyHint();
    }

    void UpdateDataModes()
    {
        const bool inFile = m_inputFormat->GetSelection() == FileFormat;
        const bool outFile = m_outputFormat->GetSelection() == FileFormat;
        m_inputPath->Show(inFile); m_browseInput->Show(inFile); m_input->Show(!inFile);
        m_outputPath->Show(outFile); m_browseOutput->Show(outFile); m_output->Show(!outFile); Layout();
    }

    void GenerateKey(wxCommandEvent&)
    {
        try
        {
            if (Algorithm() != crypto_tool::RSA)
            {
                m_key->SetValue(EncodeText(crypto_tool::RandomBytes(
                    crypto_tool::RequiredKeySize(Algorithm(), AesKeySize())), HexFormat)); return;
            }
            wxBusyCursor busy;
            const unsigned bits = static_cast<unsigned>(2048 + 1024 * m_rsaBits->GetSelection());
            const std::pair<std::string, std::string> keys = crypto_tool::GenerateRsaKeyPair(bits);
            RsaKeysDialog dialog(this, Pem(keys.first, false), Pem(keys.second, true));
            const int result = dialog.ShowModal();
            if (result == wxID_APPLY) m_key->SetValue(Pem(keys.first, false));
            else if (result == wxID_YES) m_key->SetValue(Pem(keys.second, true));
        }
        catch (const CryptoPP::Exception& e) { Error(wxString::FromUTF8(e.what())); }
    }

    void GenerateIv(wxCommandEvent&)
    {
        m_iv->SetValue(EncodeText(crypto_tool::RandomBytes(crypto_tool::RequiredIvSize(Algorithm(), Mode())), HexFormat));
    }

    void Browse(bool input)
    {
        wxFileDialog dialog(this, input ? ITOOL_TR("Select input file") : ITOOL_TR("Select output file"), wxEmptyString, wxEmptyString,
            ITOOL_TR("All files (*.*)|*.*"), input ? (wxFD_OPEN | wxFD_FILE_MUST_EXIST) : (wxFD_SAVE | wxFD_OVERWRITE_PROMPT));
        if (dialog.ShowModal() == wxID_OK) (input ? m_inputPath : m_outputPath)->SetValue(dialog.GetPath());
    }

    void Run(wxCommandEvent&)
    {
        try
        {
            crypto_tool::Request r; r.algorithm = Algorithm(); r.mode = Mode(); r.encrypt = m_encrypt->GetValue();
            const DataFormat inFormat = static_cast<DataFormat>(m_inputFormat->GetSelection());
            r.input = inFormat == FileFormat ? ReadFile(m_inputPath->GetValue()) : DecodeText(m_input->GetValue(), inFormat);
            r.key = r.algorithm == crypto_tool::RSA ? DecodeKey(m_key->GetValue()) : DecodeText(m_key->GetValue(), HexFormat);
            r.iv = (r.algorithm == crypto_tool::RSA || r.mode == crypto_tool::ECB) ? std::string() : DecodeText(m_iv->GetValue(), HexFormat);
            r.aad = Utf8(m_aad->GetValue()); r.tagSize = TagSize(); r.padding = Padding();
            Validate(r);
            wxBusyCursor busy; const std::string result = crypto_tool::Transform(r);
            const DataFormat outFormat = static_cast<DataFormat>(m_outputFormat->GetSelection());
            if (outFormat == FileFormat) { if (m_outputPath->GetValue().empty()) throw CryptoPP::InvalidArgument("Please select an output file"); WriteFile(m_outputPath->GetValue(), result); m_output->Clear(); }
            else m_output->SetValue(EncodeText(result, outFormat));
            m_status->SetLabel(wxString::Format(ITOOL_TR("%s completed: %u -> %u bytes"), r.encrypt ? ITOOL_TR("Encryption") : ITOOL_TR("Decryption"),
                static_cast<unsigned>(r.input.size()), static_cast<unsigned>(result.size())));
        }
        catch (const CryptoPP::Exception& e) { Error(wxS("Crypto++: ") + wxString::FromUTF8(e.what())); }
        catch (const std::exception& e) { Error(wxString::FromUTF8(e.what())); }
    }

    void Validate(const crypto_tool::Request& r)
    {
        if (r.key.empty()) throw CryptoPP::InvalidArgument("Key is required");
        if (r.algorithm != crypto_tool::RSA)
        {
            const size_t required = crypto_tool::RequiredKeySize(r.algorithm, AesKeySize());
            if (r.key.size() != required) throw CryptoPP::InvalidArgument("Incorrect key length");
            const size_t iv = crypto_tool::RequiredIvSize(r.algorithm, r.mode);
            if (iv && r.iv.size() != iv) throw CryptoPP::InvalidArgument("Incorrect IV/Nonce length");
            if (!r.encrypt && r.mode == crypto_tool::GCM && r.input.size() < r.tagSize)
                throw CryptoPP::InvalidArgument("Ciphertext is shorter than the authentication tag");
            const size_t blockSize = crypto_tool::RequiredBlockSize(r.algorithm);
            if ((r.mode == crypto_tool::CBC || r.mode == crypto_tool::ECB) && r.padding == crypto_tool::NoPadding &&
                r.input.size() % blockSize != 0)
                throw CryptoPP::InvalidArgument("No Padding requires input length to be a multiple of the block size");
        }
    }

    void Swap(wxCommandEvent&)
    {
        if (m_inputFormat->GetSelection() == FileFormat || m_outputFormat->GetSelection() == FileFormat) return;
        const wxString output = m_output->GetValue(); m_output->SetValue(m_input->GetValue()); m_input->SetValue(output);
        const int f = m_inputFormat->GetSelection(); m_inputFormat->SetSelection(m_outputFormat->GetSelection()); m_outputFormat->SetSelection(f);
        if (m_encrypt->GetValue()) m_decrypt->SetValue(true); else m_encrypt->SetValue(true);
        OperationChanged();
    }

    void Copy(wxCommandEvent&)
    {
        if (m_outputFormat->GetSelection() == FileFormat) return;
        if (wxTheClipboard->Open()) { wxTheClipboard->SetData(new wxTextDataObject(m_output->GetValue())); wxTheClipboard->Close(); m_status->SetLabel(ITOOL_TR("Result copied.")); }
    }
    void Clear(wxCommandEvent&) { m_input->Clear(); m_output->Clear(); m_inputPath->Clear(); m_outputPath->Clear(); m_status->SetLabel(ITOOL_TR("Input and output cleared; key parameters retained.")); }
    void Error(const wxString& message) { m_status->SetLabel(ITOOL_TR("Operation failed.")); wxMessageBox(message, ITOOL_TR("Encryption/decryption error"), wxOK | wxICON_WARNING, this); }

    wxChoice *m_algorithm, *m_mode, *m_aesSize, *m_rsaBits, *m_tagSize, *m_padding, *m_inputFormat, *m_outputFormat;
    wxRadioButton *m_encrypt, *m_decrypt;
    wxTextCtrl *m_key, *m_iv, *m_aad, *m_input, *m_output, *m_inputPath, *m_outputPath;
    wxStaticText *m_keyLabel, *m_ivLabel, *m_aadLabel, *m_tagLabel, *m_aesSizeLabel, *m_paddingLabel, *m_paddingHint, *m_warning, *m_status;
    wxButton *m_generateKey, *m_generateIv, *m_browseInput, *m_browseOutput, *m_run;
    std::vector<size_t> m_keySizes;
    std::vector<crypto_tool::Mode> m_modeValues;
};
}

wxString CryptoToolModule::GetId() const { return wxS("crypto"); }
wxString CryptoToolModule::GetName() const { return ITOOL_TR("Encryption / Decryption"); }
wxString CryptoToolModule::GetDescription() const { return ITOOL_TR("Encrypt and decrypt data using Crypto++ algorithms"); }
wxWindow* CryptoToolModule::CreatePanel(wxWindow* parent) { return new CryptoPanel(parent); }
