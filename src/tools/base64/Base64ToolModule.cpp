#include "tools/base64/Base64ToolModule.h"
#include "core/Localization.h"

#include <algorithm>
#include <string>
#include <memory>
#include <vector>

#include <wx/panel.h>
#include <wx/button.h>
#include <wx/filedlg.h>
#include <wx/ffile.h>
#include <wx/filename.h>
#include <wx/msgdlg.h>
#include <wx/radiobut.h>
#include <wx/sizer.h>
#include <wx/statbox.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

#include <base64.h>
#include <filters.h>

namespace
{
const wxULongLong kMaximumTextSourceSize(16ULL * 1024ULL * 1024ULL);

std::unique_ptr<CryptoPP::BufferedTransformation> MakeFilter(bool encode)
{
    if (encode)
        return std::unique_ptr<CryptoPP::BufferedTransformation>(new CryptoPP::Base64Encoder(NULL, false));
    return std::unique_ptr<CryptoPP::BufferedTransformation>(new CryptoPP::Base64Decoder(NULL));
}

void ConvertFile(const wxString& source, const wxString& target, bool encode)
{
    wxFFile input(source, wxS("rb"));
    wxFFile output(target, wxS("wb"));
    if (!input.IsOpened() || !output.IsOpened())
        throw CryptoPP::InvalidArgument("Unable to open input or output file");
    std::unique_ptr<CryptoPP::BufferedTransformation> filter = MakeFilter(encode);
    std::vector<CryptoPP::byte> inputBuffer(64 * 1024), outputBuffer(96 * 1024);
    for (;;)
    {
        const size_t count = input.Read(&inputBuffer[0], inputBuffer.size());
        if (count) filter->Put(&inputBuffer[0], count);
        while (filter->MaxRetrievable())
        {
            const size_t available = static_cast<size_t>(filter->MaxRetrievable());
            const size_t take = std::min(available, outputBuffer.size());
            filter->Get(&outputBuffer[0], take);
            if (output.Write(&outputBuffer[0], take) != take)
                throw CryptoPP::InvalidArgument("Unable to write output file");
        }
        if (count < inputBuffer.size()) break;
    }
    filter->MessageEnd();
    while (filter->MaxRetrievable())
    {
        const size_t available = static_cast<size_t>(filter->MaxRetrievable());
        const size_t take = std::min(available, outputBuffer.size());
        filter->Get(&outputBuffer[0], take);
        if (output.Write(&outputBuffer[0], take) != take)
            throw CryptoPP::InvalidArgument("Unable to write output file");
    }
    if (!output.Close()) throw CryptoPP::InvalidArgument("Unable to close output file");
}

std::string ConvertString(const std::string& source, bool encode)
{
    std::string result;
    CryptoPP::BufferedTransformation* sink = new CryptoPP::StringSink(result);
    CryptoPP::BufferedTransformation* filter = encode
        ? static_cast<CryptoPP::BufferedTransformation*>(new CryptoPP::Base64Encoder(sink, false))
        : static_cast<CryptoPP::BufferedTransformation*>(new CryptoPP::Base64Decoder(sink));
    CryptoPP::StringSource(source, true, filter);
    return result;
}

std::string ReadFile(const wxString& path)
{
    wxFFile file(path, wxS("rb"));
    if (!file.IsOpened() || file.Length() < 0)
        throw CryptoPP::InvalidArgument("Unable to read input file");
    std::string result(static_cast<size_t>(file.Length()), '\0');
    if (!result.empty() && file.Read(&result[0], result.size()) != result.size())
        throw CryptoPP::InvalidArgument("Unable to read input file");
    return result;
}

void WriteFile(const wxString& path, const std::string& bytes)
{
    wxFFile file(path, wxS("wb"));
    if (!file.IsOpened() || (!bytes.empty() && file.Write(bytes.data(), bytes.size()) != bytes.size()) ||
        !file.Close())
        throw CryptoPP::InvalidArgument("Unable to write output file");
}

bool BytesToUtf8(const std::string& bytes, wxString& text)
{
    if (bytes.empty()) { text.clear(); return true; }
    size_t length = 0;
    wxWCharBuffer converted = wxConvUTF8.cMB2WC(bytes.data(), bytes.size(), &length);
    if (!converted) return false;
    text.assign(converted.data(), length);
    return true;
}

class Base64Panel : public wxPanel
{
public:
    explicit Base64Panel(wxWindow* parent) : wxPanel(parent)
    {
        wxBoxSizer* root = new wxBoxSizer(wxVERTICAL);

        wxStaticBoxSizer* inputBox = new wxStaticBoxSizer(wxVERTICAL, this, ITOOL_TR("Input"));
        wxBoxSizer* inputFileRow = new wxBoxSizer(wxHORIZONTAL);
        m_inputFileMode = new wxRadioButton(this, wxID_ANY, ITOOL_TR("From file:"),
                                             wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
        m_inputFile = new wxTextCtrl(this, wxID_ANY);
        m_browseInput = new wxButton(this, wxID_ANY, wxS("..."), wxDefaultPosition, wxSize(36, -1));
        inputFileRow->Add(m_inputFileMode, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
        inputFileRow->Add(m_inputFile, 1, wxRIGHT, 5);
        inputFileRow->Add(m_browseInput);
        inputBox->Add(inputFileRow, 0, wxEXPAND | wxALL, 5);
        m_inputTextMode = new wxRadioButton(this, wxID_ANY, ITOOL_TR("From text box:"));
        m_inputTextMode->SetValue(true);
        inputBox->Add(m_inputTextMode, 0, wxLEFT | wxRIGHT | wxBOTTOM, 5);
        m_inputText = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
                                     wxTE_MULTILINE | wxTE_RICH2);
        inputBox->Add(m_inputText, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 5);
        root->Add(inputBox, 1, wxEXPAND | wxALL, 8);

        wxStaticBoxSizer* outputBox = new wxStaticBoxSizer(wxVERTICAL, this, ITOOL_TR("Output"));
        wxBoxSizer* outputFileRow = new wxBoxSizer(wxHORIZONTAL);
        m_outputFileMode = new wxRadioButton(this, wxID_ANY, ITOOL_TR("Write to file:"),
                                              wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
        m_outputFile = new wxTextCtrl(this, wxID_ANY);
        m_browseOutput = new wxButton(this, wxID_ANY, wxS("..."), wxDefaultPosition, wxSize(36, -1));
        outputFileRow->Add(m_outputFileMode, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
        outputFileRow->Add(m_outputFile, 1, wxRIGHT, 5);
        outputFileRow->Add(m_browseOutput);
        outputBox->Add(outputFileRow, 0, wxEXPAND | wxALL, 5);
        m_outputTextMode = new wxRadioButton(this, wxID_ANY, ITOOL_TR("Output to text box:"));
        m_outputTextMode->SetValue(true);
        outputBox->Add(m_outputTextMode, 0, wxLEFT | wxRIGHT | wxBOTTOM, 5);
        m_outputText = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
                                      wxTE_MULTILINE | wxTE_READONLY | wxTE_RICH2);
        outputBox->Add(m_outputText, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 5);
        root->Add(outputBox, 1, wxEXPAND | wxLEFT | wxRIGHT, 8);

        wxBoxSizer* actionRow = new wxBoxSizer(wxHORIZONTAL);
        m_encode = new wxRadioButton(this, wxID_ANY, ITOOL_TR("Convert to Base64"),
                                     wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
        m_decode = new wxRadioButton(this, wxID_ANY, ITOOL_TR("Decode from Base64"));
        m_encode->SetValue(true);
        wxButton* convert = new wxButton(this, wxID_ANY, ITOOL_TR("Convert"), wxDefaultPosition, wxSize(110, -1));
        wxButton* cancel = new wxButton(this, wxID_ANY, ITOOL_TR("Cancel"), wxDefaultPosition, wxSize(110, -1));
        actionRow->Add(m_encode, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 18);
        actionRow->Add(m_decode, 0, wxALIGN_CENTER_VERTICAL);
        actionRow->AddStretchSpacer();
        actionRow->Add(convert, 0, wxRIGHT, 8);
        actionRow->Add(cancel);
        root->Add(actionRow, 0, wxEXPAND | wxALL, 8);
        SetSizer(root);

        m_browseInput->Bind(wxEVT_BUTTON, &Base64Panel::BrowseInput, this);
        m_browseOutput->Bind(wxEVT_BUTTON, &Base64Panel::BrowseOutput, this);
        convert->Bind(wxEVT_BUTTON, &Base64Panel::Convert, this);
        cancel->Bind(wxEVT_BUTTON, &Base64Panel::Cancel, this);
        m_inputFileMode->Bind(wxEVT_RADIOBUTTON, &Base64Panel::ModeChanged, this);
        m_inputTextMode->Bind(wxEVT_RADIOBUTTON, &Base64Panel::ModeChanged, this);
        m_outputFileMode->Bind(wxEVT_RADIOBUTTON, &Base64Panel::ModeChanged, this);
        m_outputTextMode->Bind(wxEVT_RADIOBUTTON, &Base64Panel::ModeChanged, this);
        UpdateModes();
    }

private:
    void UpdateModes()
    {
        const bool inputFile = m_inputFileMode->GetValue();
        m_inputFile->Enable(inputFile); m_browseInput->Enable(inputFile); m_inputText->Enable(!inputFile);
        const bool outputFile = m_outputFileMode->GetValue();
        m_outputFile->Enable(outputFile); m_browseOutput->Enable(outputFile); m_outputText->Enable(!outputFile);
    }

    void ModeChanged(wxCommandEvent&) { UpdateModes(); }

    void BrowseInput(wxCommandEvent&)
    {
        wxFileDialog dialog(this, ITOOL_TR("Select input file"), wxEmptyString, wxEmptyString,
                            ITOOL_TR("All files (*.*)|*.*"), wxFD_OPEN | wxFD_FILE_MUST_EXIST);
        if (dialog.ShowModal() == wxID_OK) m_inputFile->SetValue(dialog.GetPath());
    }

    void BrowseOutput(wxCommandEvent&)
    {
        wxFileDialog dialog(this, ITOOL_TR("Select output file"), wxEmptyString, wxEmptyString,
                            ITOOL_TR("All files (*.*)|*.*"), wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
        if (dialog.ShowModal() == wxID_OK) m_outputFile->SetValue(dialog.GetPath());
    }

    bool Validate(wxString& error) const
    {
        if (m_inputFileMode->GetValue() && !wxFileName::FileExists(m_inputFile->GetValue()))
            { error = ITOOL_TR("Select a valid input file."); return false; }
        if (!m_inputFileMode->GetValue() && m_inputText->GetValue().empty())
            { error = ITOOL_TR("Enter the text to convert."); return false; }
        if (m_outputFileMode->GetValue() && m_outputFile->GetValue().empty())
            { error = ITOOL_TR("Select an output file."); return false; }
        if (m_inputFileMode->GetValue() && m_outputFileMode->GetValue())
        {
            wxFileName source(m_inputFile->GetValue()), target(m_outputFile->GetValue());
            source.Normalize(wxPATH_NORM_ABSOLUTE); target.Normalize(wxPATH_NORM_ABSOLUTE);
            if (source.SameAs(target)) { error = ITOOL_TR("The input and output files cannot be the same."); return false; }
        }
        if (m_inputFileMode->GetValue() && !m_outputFileMode->GetValue() &&
            wxFileName(m_inputFile->GetValue()).GetSize() > kMaximumTextSourceSize)
            { error = ITOOL_TR("The file exceeds 16 MiB. Write to a file to avoid excessive text-box memory use."); return false; }
        return true;
    }

    void Convert(wxCommandEvent&)
    {
        wxString error;
        if (!Validate(error)) { ShowError(error); return; }
        wxBusyCursor busy;
        try
        {
            const bool encode = m_encode->GetValue();
            if (m_inputFileMode->GetValue() && m_outputFileMode->GetValue())
            {
                ConvertFile(m_inputFile->GetValue(), m_outputFile->GetValue(), encode);
            }
            else
            {
                std::string input;
                if (m_inputFileMode->GetValue())
                    input = ReadFile(m_inputFile->GetValue());
                else
                {
                    wxCharBuffer utf8 = m_inputText->GetValue().utf8_str();
                    input.assign(utf8.data(), utf8.length());
                }
                const std::string result = ConvertString(input, encode);
                if (m_outputFileMode->GetValue())
                    WriteFile(m_outputFile->GetValue(), result);
                else
                {
                    wxString output;
                    if (!BytesToUtf8(result, output))
                    {
                        ShowError(ITOOL_TR("The decoded result is not UTF-8 text. Output to a file to preserve the binary data."));
                        return;
                    }
                    m_outputText->SetValue(output);
                }
            }
            if (m_outputFileMode->GetValue())
                wxMessageBox(ITOOL_TR("Conversion completed."), wxS("Base64"), wxOK | wxICON_INFORMATION, this);
        }
        catch (const CryptoPP::Exception& exception)
        {
            ShowError(ITOOL_TR("Crypto++ conversion failed: ") + wxString::FromUTF8(exception.what()));
        }
    }

    void Cancel(wxCommandEvent&)
    {
        m_inputFile->Clear(); m_inputText->Clear();
        m_outputFile->Clear(); m_outputText->Clear();
    }

    void ShowError(const wxString& error)
    {
        wxMessageBox(error, ITOOL_TR("Base64 error"), wxOK | wxICON_WARNING, this);
    }

    wxRadioButton *m_inputFileMode, *m_inputTextMode, *m_outputFileMode, *m_outputTextMode;
    wxRadioButton *m_encode, *m_decode;
    wxTextCtrl *m_inputFile, *m_inputText, *m_outputFile, *m_outputText;
    wxButton *m_browseInput, *m_browseOutput;
};
}

wxString Base64ToolModule::GetId() const { return wxS("base64"); }
wxString Base64ToolModule::GetName() const { return ITOOL_TR("Base64"); }
wxString Base64ToolModule::GetDescription() const { return ITOOL_TR("Convert files or UTF-8 text to and from Base64"); }
wxWindow* Base64ToolModule::CreatePanel(wxWindow* parent) { return new Base64Panel(parent); }
