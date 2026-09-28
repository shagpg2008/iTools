#define CRYPTOPP_ENABLE_NAMESPACE_WEAK 1
#include "tools/hash/HashToolModule.h"
#include "core/Localization.h"

#include <memory>
#include <vector>

#include <wx/button.h>
#include <wx/dialog.h>
#include <wx/dnd.h>
#include <wx/filedlg.h>
#include <wx/filename.h>
#include <wx/grid.h>
#include <wx/msgdlg.h>
#include <wx/radiobut.h>
#include <wx/renderer.h>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/thread.h>

#include "tools/crc/MappedFile.h"
#include <blake2.h>
#include <keccak.h>
#include <md5.h>
#include <ripemd.h>
#include <sha.h>
#include <sha3.h>
#include <sm3.h>

namespace
{
enum InputMode { InputHex, InputText, InputFile };
const char* const kNames[] = {
    "MD5", "SHA1", "SHA2-224", "SHA2-256", "SHA2-384", "SHA2-512",
    "SHA3-224", "SHA3-256", "SHA3-384", "SHA3-512",
    "Keccak-224", "Keccak-256", "Keccak-384", "Keccak-512",
    "RIPEMD-128", "RIPEMD-160", "RIPEMD-256", "RIPEMD-320",
    "BLAKE2s-256", "BLAKE2b-256", "BLAKE2b-512", "SM3"
};
const int kCount = sizeof(kNames) / sizeof(kNames[0]);

std::unique_ptr<CryptoPP::HashTransformation> MakeHash(int index)
{
    using namespace CryptoPP;
    switch (index) {
    case 0: return std::unique_ptr<HashTransformation>(new Weak::MD5);
    case 1: return std::unique_ptr<HashTransformation>(new SHA1);
    case 2: return std::unique_ptr<HashTransformation>(new SHA224);
    case 3: return std::unique_ptr<HashTransformation>(new SHA256);
    case 4: return std::unique_ptr<HashTransformation>(new SHA384);
    case 5: return std::unique_ptr<HashTransformation>(new SHA512);
    case 6: return std::unique_ptr<HashTransformation>(new SHA3_224);
    case 7: return std::unique_ptr<HashTransformation>(new SHA3_256);
    case 8: return std::unique_ptr<HashTransformation>(new SHA3_384);
    case 9: return std::unique_ptr<HashTransformation>(new SHA3_512);
    case 10: return std::unique_ptr<HashTransformation>(new Keccak_224);
    case 11: return std::unique_ptr<HashTransformation>(new Keccak_256);
    case 12: return std::unique_ptr<HashTransformation>(new Keccak_384);
    case 13: return std::unique_ptr<HashTransformation>(new Keccak_512);
    case 14: return std::unique_ptr<HashTransformation>(new RIPEMD128);
    case 15: return std::unique_ptr<HashTransformation>(new RIPEMD160);
    case 16: return std::unique_ptr<HashTransformation>(new RIPEMD256);
    case 17: return std::unique_ptr<HashTransformation>(new RIPEMD320);
    case 18: return std::unique_ptr<HashTransformation>(new BLAKE2s(false, 32));
    case 19: return std::unique_ptr<HashTransformation>(new BLAKE2b(false, 32));
    case 20: return std::unique_ptr<HashTransformation>(new BLAKE2b(false, 64));
    case 21: return std::unique_ptr<HashTransformation>(new SM3);
    }
    return std::unique_ptr<HashTransformation>();
}

wxString FinishHash(CryptoPP::HashTransformation& hash)
{
    std::vector<CryptoPP::byte> digest(hash.DigestSize());
    hash.Final(&digest[0]);
    static const wxChar hex[] = wxS("0123456789ABCDEF");
    wxString result;
    result.reserve(digest.size() * 2);
    for (size_t i = 0; i < digest.size(); ++i) {
        result += hex[digest[i] >> 4]; result += hex[digest[i] & 15];
    }
    return result;
}

bool ParseHex(const wxString& source, std::vector<CryptoPP::byte>& data, wxString& error)
{
    wxString clean;
    for (size_t i=0; i<source.length(); ++i) {
        const wxChar c=source[i];
        if (wxIsspace(c) || c==':' || c=='-' || c==',') continue;
        if (!wxIsxdigit(c)) { error=ITOOL_TR("Hex input contains an invalid character."); return false; }
        clean += c;
    }
    if (clean.empty() || clean.length()%2) { error=ITOOL_TR("Hex data must contain an even number of hexadecimal digits."); return false; }
    for (size_t i=0;i<clean.length();i+=2) {
        unsigned long v=0; if(!clean.Mid(i,2).ToULong(&v,16)) return false;
        data.push_back(static_cast<CryptoPP::byte>(v));
    }
    return true;
}

struct ActiveHash { int row; std::unique_ptr<CryptoPP::HashTransformation> hash; wxString value; };
wxDEFINE_EVENT(EVT_HASH_FILE_COMPLETE, wxThreadEvent);

class FileHashThread : public wxThread
{
public:
    FileHashThread(wxEvtHandler* owner, const wxString& path, std::vector<ActiveHash>& active)
        : wxThread(wxTHREAD_JOINABLE), m_owner(owner), m_path(path), m_ok(false), m_size(0) { m_active.swap(active); }
    bool Ok() const { return m_ok; } const wxString& Error() const { return m_error; }
    std::vector<ActiveHash>& Results() { return m_active; }
protected:
    ExitCode Entry() wxOVERRIDE {
        m_ok=VisitMappedFile(m_path,[this](const u8* p,size_t n){
            for(size_t i=0;i<m_active.size();++i)
                m_active[i].hash->Update(p,n);
            return true;
        },m_size,m_error);
        if(m_ok) for(size_t i=0;i<m_active.size();++i) m_active[i].value=FinishHash(*m_active[i].hash);
        wxQueueEvent(m_owner,new wxThreadEvent(EVT_HASH_FILE_COMPLETE)); return static_cast<ExitCode>(0);
    }
private:
    wxEvtHandler* m_owner; wxString m_path; std::vector<ActiveHash> m_active;
    bool m_ok; u64 m_size; wxString m_error;
};

class HashGrid : public wxGrid
{
public:
    explicit HashGrid(wxWindow* parent) : wxGrid(parent, wxID_ANY) {}
protected:
    void DrawColLabel(wxDC& dc, int column) wxOVERRIDE {
        wxGrid::DrawColLabel(dc,column);
        if(column!=0||GetNumberRows()==0)return;
        bool any=false,all=true;
        for(int r=0;r<GetNumberRows();++r){bool checked=GetCellValue(r,0)==wxS("1");any|=checked;all&=checked;}
        int flags=all?static_cast<int>(wxCONTROL_CHECKED):
                      (any?static_cast<int>(wxCONTROL_UNDETERMINED):0);
        wxRendererNative& renderer=wxRendererNative::Get();
        wxSize size=renderer.GetCheckBoxSize(GetGridColLabelWindow(),flags);
        wxRect label(GetColLeft(column),0,GetColSize(column),GetColLabelSize());
        wxRect box(label.x+(label.width-size.x)/2,label.y+(label.height-size.y)/2,size.x,size.y);
        renderer.DrawCheckBox(GetGridColLabelWindow(),dc,box,flags);
    }
};

class HashPanel : public wxPanel
{
public:
    explicit HashPanel(wxWindow* parent) : wxPanel(parent),m_mode(InputHex),m_thread(NULL),m_wait(NULL) { Build(); }
    ~HashPanel(){ if(m_thread){m_thread->Wait();delete m_thread;} }
private:
    class DropTarget:public wxFileDropTarget { public: explicit DropTarget(HashPanel* p):m_p(p){} bool OnDropFiles(wxCoord,wxCoord,const wxArrayString& f) wxOVERRIDE { return m_p->Drop(f); } private: HashPanel* m_p; };
    void Build() {
        wxBoxSizer* root=new wxBoxSizer(wxVERTICAL), *head=new wxBoxSizer(wxHORIZONTAL);
        wxRadioButton* hex=new wxRadioButton(this,wxID_ANY,wxS("Hex"),wxDefaultPosition,wxDefaultSize,wxRB_GROUP);
        wxRadioButton* text=new wxRadioButton(this,wxID_ANY,wxS("Text")); wxRadioButton* file=new wxRadioButton(this,wxID_ANY,wxS("File"));
        head->Add(hex,0,wxRIGHT|wxALIGN_CENTER_VERTICAL,8);head->Add(text,0,wxRIGHT|wxALIGN_CENTER_VERTICAL,8);head->Add(file,0,wxRIGHT|wxALIGN_CENTER_VERTICAL,10);
        m_path=new wxTextCtrl(this,wxID_ANY);m_browse=new wxButton(this,wxID_ANY,wxS("..."),wxDefaultPosition,wxSize(36,-1));head->Add(m_path,1,wxRIGHT,5);head->Add(m_browse);root->Add(head,0,wxEXPAND|wxALL,8);
        m_input=new wxTextCtrl(this,wxID_ANY,wxS("31 32 33 34 35 36 37 38 39"),wxDefaultPosition,wxSize(-1,150),wxTE_MULTILINE);root->Add(m_input,1,wxEXPAND|wxLEFT|wxRIGHT|wxBOTTOM,8);
        m_grid=new HashGrid(this);m_grid->CreateGrid(kCount,3);m_grid->SetRowLabelSize(0);m_grid->SetColLabelValue(0,wxEmptyString);m_grid->SetColLabelValue(1,wxS("Name"));m_grid->SetColLabelValue(2,wxS("Hash Value"));m_grid->SetColFormatBool(0);m_grid->SetColSize(0,30);m_grid->SetColSize(1,130);m_grid->SetColSize(2,520);
        for(int i=0;i<kCount;++i){m_grid->SetCellAlignment(i,0,wxALIGN_CENTER,wxALIGN_CENTER);m_grid->SetCellValue(i,0,(i==3)?wxS("1"):wxS("0"));m_grid->SetCellValue(i,1,wxString::FromUTF8(kNames[i]));m_grid->SetReadOnly(i,1);m_grid->SetReadOnly(i,2);}
        root->Add(m_grid,2,wxEXPAND|wxLEFT|wxRIGHT,8);wxBoxSizer* foot=new wxBoxSizer(wxHORIZONTAL);m_calc=new wxButton(this,wxID_ANY,ITOOL_TR("Calculate"));wxButton* clear=new wxButton(this,wxID_CLEAR,ITOOL_TR("Clear results"));m_status=new wxStaticText(this,wxID_ANY,ITOOL_TR("Select algorithms, then calculate."));foot->Add(m_calc,0,wxRIGHT,8);foot->Add(clear,0,wxRIGHT,12);foot->Add(m_status,1,wxALIGN_CENTER_VERTICAL);root->Add(foot,0,wxEXPAND|wxALL,8);SetSizer(root);
        hex->Bind(wxEVT_RADIOBUTTON,[this](wxCommandEvent&){m_mode=InputHex;Mode();});text->Bind(wxEVT_RADIOBUTTON,[this](wxCommandEvent&){m_mode=InputText;Mode();});file->Bind(wxEVT_RADIOBUTTON,[this](wxCommandEvent&){m_mode=InputFile;Mode();});m_browse->Bind(wxEVT_BUTTON,&HashPanel::Browse,this);m_calc->Bind(wxEVT_BUTTON,&HashPanel::Calculate,this);clear->Bind(wxEVT_BUTTON,[this](wxCommandEvent&){for(int i=0;i<kCount;++i)m_grid->SetCellValue(i,2,wxEmptyString);});m_grid->Bind(wxEVT_GRID_LABEL_LEFT_CLICK,&HashPanel::GridLabel,this);m_grid->Bind(wxEVT_GRID_CELL_LEFT_CLICK,&HashPanel::GridCell,this);Bind(wxEVT_SIZE,&HashPanel::PanelResized,this);SetDropTarget(new DropTarget(this));m_path->SetDropTarget(new DropTarget(this));m_grid->GetGridWindow()->SetDropTarget(new DropTarget(this));Bind(EVT_HASH_FILE_COMPLETE,&HashPanel::Complete,this);Mode();
    }
    void PanelResized(wxSizeEvent& event){event.Skip();const int stableWidth=GetClientSize().x-16-wxSystemSettings::GetMetric(wxSYS_VSCROLL_X,m_grid)-4;const int available=stableWidth-m_grid->GetColSize(0)-m_grid->GetColSize(1);if(available>120&&m_grid->GetColSize(2)!=available)m_grid->SetColSize(2,available);}
    void Mode(){bool f=m_mode==InputFile;m_path->Enable(f);m_browse->Enable(f);m_input->Enable(!f);if(m_mode==InputHex)m_input->SetHint(ITOOL_TR("Hex: 31 32 33 or 313233"));else if(m_mode==InputText)m_input->SetHint(ITOOL_TR("Text is hashed using UTF-8 encoding"));Layout();}
    void GridLabel(wxGridEvent& e){if(e.GetCol()!=0){e.Skip();return;}bool all=true;for(int i=0;i<kCount;++i)all&=m_grid->GetCellValue(i,0)==wxS("1");for(int i=0;i<kCount;++i)m_grid->SetCellValue(i,0,all?wxS("0"):wxS("1"));m_grid->GetGridColLabelWindow()->Refresh();}
    void GridCell(wxGridEvent& e){if(e.GetCol()!=0||e.GetRow()<0){e.Skip();return;}const int row=e.GetRow();m_grid->SetGridCursor(row,0);m_grid->SetCellValue(row,0,m_grid->GetCellValue(row,0)==wxS("1")?wxS("0"):wxS("1"));m_grid->ForceRefresh();m_grid->GetGridColLabelWindow()->Refresh(false);}
    bool Drop(const wxArrayString& f){if(f.empty()||!wxFileName::FileExists(f[0]))return false;m_mode=InputFile;m_path->SetValue(f[0]);Mode();return true;}
    void Browse(wxCommandEvent&){wxFileDialog d(this,ITOOL_TR("Select a file to hash"),wxEmptyString,wxEmptyString,ITOOL_TR("All files (*.*)|*.*"),wxFD_OPEN|wxFD_FILE_MUST_EXIST);if(d.ShowModal()==wxID_OK){m_path->SetValue(d.GetPath());}}
    bool Active(std::vector<ActiveHash>& a){for(int i=0;i<kCount;++i)if(m_grid->GetCellValue(i,0)==wxS("1")){ActiveHash h;h.row=i;h.hash=MakeHash(i);a.push_back(std::move(h));}if(a.empty()){wxMessageBox(ITOOL_TR("Select at least one hash algorithm."),wxS("Hash"),wxOK|wxICON_INFORMATION,this);return false;}return true;}
    void Calculate(wxCommandEvent&){std::vector<ActiveHash>a;if(!Active(a))return;if(m_mode==InputFile){wxString p=m_path->GetValue();if(!wxFileName::FileExists(p)){wxMessageBox(ITOOL_TR("Select a valid file."));return;}m_calc->Disable();m_thread=new FileHashThread(this,p,a);if(m_thread->Run()!=wxTHREAD_NO_ERROR){delete m_thread;m_thread=NULL;m_calc->Enable();return;}if(wxFileName(p).GetSize()>wxULongLong(50ULL*1024*1024)){m_wait=new wxDialog(this,wxID_ANY,ITOOL_TR("Hash calculation"),wxDefaultPosition,wxDefaultSize,wxDEFAULT_DIALOG_STYLE);wxBoxSizer*s=new wxBoxSizer(wxVERTICAL);s->Add(new wxStaticText(m_wait,wxID_ANY,ITOOL_TR("Please wait ...")),0,wxALL,24);m_wait->SetSizerAndFit(s);m_wait->CentreOnParent();m_wait->ShowModal();delete m_wait;m_wait=NULL;}return;}
        std::vector<CryptoPP::byte>d;wxString e;if(m_mode==InputHex){if(!ParseHex(m_input->GetValue(),d,e)){wxMessageBox(e);return;}}else{wxCharBuffer b=m_input->GetValue().utf8_str();if(b.length())d.assign(reinterpret_cast<const CryptoPP::byte*>(b.data()),reinterpret_cast<const CryptoPP::byte*>(b.data())+b.length());}for(size_t i=0;i<a.size();++i){if(!d.empty())a[i].hash->Update(&d[0],d.size());m_grid->SetCellValue(a[i].row,2,FinishHash(*a[i].hash));}m_status->SetLabel(ITOOL_TR("Calculation completed."));}
    void Complete(wxThreadEvent&){m_thread->Wait();if(m_thread->Ok()){std::vector<ActiveHash>&r=m_thread->Results();for(size_t i=0;i<r.size();++i)m_grid->SetCellValue(r[i].row,2,r[i].value);m_status->SetLabel(ITOOL_TR("File hash calculation completed."));}else wxMessageBox(m_thread->Error(),ITOOL_TR("Hash error"),wxOK|wxICON_ERROR,this);delete m_thread;m_thread=NULL;m_calc->Enable();if(m_wait&&m_wait->IsModal())m_wait->EndModal(wxID_OK);}
    InputMode m_mode;wxTextCtrl *m_input,*m_path;wxButton *m_browse,*m_calc;wxGrid*m_grid;wxStaticText*m_status;FileHashThread*m_thread;wxDialog*m_wait;
};
}

wxString HashToolModule::GetId() const{return wxS("hash");}
wxString HashToolModule::GetName() const{return ITOOL_TR("Hash checker");}
wxString HashToolModule::GetDescription() const{return ITOOL_TR("Calculate hashes for text, hexadecimal data, or files");}
wxWindow* HashToolModule::CreatePanel(wxWindow* parent){return new HashPanel(parent);}
