#include "TFile.h"
#include "TROOT.h"
#include <TApplication.h>
#include <TString.h>
#include <TGWindow.h>
#include <TGFrame.h>
#include <TGNumberEntry.h>
#include <TGTextEntry.h>
#include <TGLabel.h>
#include <TGTextView.h>
#include <TGLayout.h>
#include <TGClient.h>
#include <TGMsgBox.h>
#include "TSystem.h"
#include "biascontrol.h"

#include <iostream>
#include <fstream>
#include <ctime>

MyMainFrame::MyMainFrame(const TGWindow *p, UInt_t w, UInt_t h)
{
  // Create a main frame
  fMain = new TGMainFrame(p, w, h);
  fMain->Connect("CloseWindow()", "MyMainFrame", this, "DoClose()");
  fMain->DontCallClose();

  // Create canvas widget
  fHor0 = new TGHorizontalFrame(fMain, 1024, 20);
  fHor0b = new TGHorizontalFrame(fMain, 1024, 20);

  TGVerticalFrame *fVer0 = new TGVerticalFrame(fHor0b, 0.3 * w, 10);
  TGVerticalFrame *fVer1 = new TGVerticalFrame(fHor0b, 0.7 * w, 10);

  fStatusBar = new TGTextView(fVer1, 500, 150);
  std::time_t result = std::time(nullptr);
  fStatusBar->AddLine(std::asctime(std::localtime(&result)));
  fStatusBar->AddLine("Starting bias ps control ...");

  fHor1 = new TGHorizontalFrame(fVer0, 1024, 20);
  fHor1b = new TGHorizontalFrame(fVer0, 1024, 20);
  fHor2 = new TGHorizontalFrame(fVer0, 1024, 20);
  fHor3 = new TGHorizontalFrame(fVer0, 1024, 20);

  fPing = new TGTextButton(fHor0, "&Ping");
  fPing->Connect("Clicked()", "MyMainFrame", this, "DoPing()");
  fHor0->AddFrame(fPing, new TGLayoutHints(kLHintsCenterX, 5, 5, 3, 4));

  fStatus = new TGTextButton(fHor0, "&Status");
  fStatus->Connect("Clicked()", "MyMainFrame", this, "DoStatus()");
  fHor0->AddFrame(fStatus, new TGLayoutHints(kLHintsCenterX, 5, 5, 3, 4));

  fClear = new TGTextButton(fHor0, "&Clear");
  fClear->Connect("Clicked()", "MyMainFrame", this, "DoClear()");
  fHor0->AddFrame(fClear, new TGLayoutHints(kLHintsCenterX, 5, 5, 3, 4));

  fExit = new TGTextButton(fHor0, "&Exit");
  fExit->Connect("Clicked()", "MyMainFrame", this, "DoClose()");
  fHor0->AddFrame(fExit, new TGLayoutHints(kLHintsCenterX, 5, 5, 3, 4));

  ipLabel = new TGLabel(fHor1, "IP:");
  fHor1->AddFrame(ipLabel, new TGLayoutHints(kLHintsLeft | kLHintsCenterY, 5, 2, 2, 2));
  fIP = new TGTextEntry(fHor1, "192.168.1.242");
  fHor1->AddFrame(fIP, new TGLayoutHints(kLHintsCenterX | kLHintsCenterY, 5, 5, 5, 5));

  portLabel = new TGLabel(fHor1, "Port:");
  fHor1->AddFrame(portLabel, new TGLayoutHints(kLHintsLeft | kLHintsCenterY, 5, 2, 2, 2));
  fPORT = new TGNumberEntry(fHor1, 80, 10, -1, TGNumberFormat::kNESInteger, TGNumberFormat::kNEANonNegative, TGNumberFormat::kNELNoLimits, 0, 1);
  fHor1->AddFrame(fPORT, new TGLayoutHints(kLHintsCenterX | kLHintsCenterY, 5, 5, 5, 5));

  idLabel = new TGLabel(fHor1b, "ID:");
  fHor1b->AddFrame(idLabel, new TGLayoutHints(kLHintsLeft | kLHintsCenterY, 5, 2, 2, 2));
  fID = new TGNumberEntry(fHor1b, 118, 10, -1, TGNumberFormat::kNESInteger, TGNumberFormat::kNEANonNegative, TGNumberFormat::kNELLimitMinMax, 100, 999);
  fHor1b->AddFrame(fID, new TGLayoutHints(kLHintsCenterX | kLHintsCenterY, 5, 5, 5, 5));

  voltageLabel = new TGLabel(fHor1b, "Bias:");
  fHor1b->AddFrame(voltageLabel, new TGLayoutHints(kLHintsLeft | kLHintsCenterY, 5, 2, 2, 2));
  fVOLT = new TGNumberEntry(fHor1b, 50, 10, -1, TGNumberFormat::kNESInteger, TGNumberFormat::kNEANonNegative, TGNumberFormat::kNELLimitMinMax, 20, 80);
  fHor1b->AddFrame(fVOLT, new TGLayoutHints(kLHintsCenterX | kLHintsCenterY, 5, 5, 5, 5));

  fBiasON = new TGTextButton(fHor2, "&Bias ON ALL");
  fBiasON->Connect("Clicked()", "MyMainFrame", this, "DoBiasON()");
  fHor2->AddFrame(fBiasON, new TGLayoutHints(kLHintsCenterX, 5, 5, 3, 4));

  fBiasOFF = new TGTextButton(fHor2, "&Bias OFF ALL");
  fBiasOFF->Connect("Clicked()", "MyMainFrame", this, "DoBiasOFF()");
  fHor2->AddFrame(fBiasOFF, new TGLayoutHints(kLHintsCenterX, 5, 5, 3, 4));

  fBiasUPall = new TGTextButton(fHor2, "&Bias UP ALL");
  fBiasUPall->Connect("Clicked()", "MyMainFrame", this, "DoBiasUPall()");
  fHor2->AddFrame(fBiasUPall, new TGLayoutHints(kLHintsCenterX, 5, 5, 3, 4));

  fBiasDWNall = new TGTextButton(fHor2, "&Bias DWN ALL");
  fBiasDWNall->Connect("Clicked()", "MyMainFrame", this, "DoBiasDWNall()");
  fHor2->AddFrame(fBiasDWNall, new TGLayoutHints(kLHintsCenterX, 5, 5, 3, 4));

  fSetBias = new TGTextButton(fHor1b, "&Set Bias");
  fSetBias->Connect("Clicked()", "MyMainFrame", this, "DoSetBias()");
  fHor1b->AddFrame(fSetBias, new TGLayoutHints(kLHintsCenterX | kLHintsCenterY, 5, 5, 3, 4));

  fVer0->AddFrame(fHor1, new TGLayoutHints(kLHintsCenterX | kLHintsCenterY, 2, 2, 5, 1));
  fVer0->AddFrame(fHor1b, new TGLayoutHints(kLHintsCenterX | kLHintsCenterY, 2, 2, 5, 1));
  fVer0->AddFrame(fHor2, new TGLayoutHints(kLHintsCenterX | kLHintsCenterY, 2, 2, 5, 1));
  fVer0->AddFrame(fHor3, new TGLayoutHints(kLHintsExpandX | kLHintsCenterX, 2, 2, 5, 1));
  fVer1->AddFrame(fStatusBar, new TGLayoutHints(kLHintsCenterX | kLHintsBottom | kLHintsLeft | kLHintsExpandY, 5, 5, 2, 2));

  fHor0b->AddFrame(fVer0, new TGLayoutHints(kLHintsCenterX | kLHintsExpandX | kLHintsExpandY, 2, 2, 5, 1));
  fHor0b->AddFrame(fVer1, new TGLayoutHints(kLHintsCenterX | kLHintsExpandX | kLHintsExpandY, 2, 2, 5, 1));
  fMain->AddFrame(fHor0, new TGLayoutHints(kLHintsCenterX, 2, 2, 5, 1));
  fMain->AddFrame(fHor0b, new TGLayoutHints(kLHintsExpandX | kLHintsCenterX, 2, 2, 5, 1));

  fMain->SetCleanup(kDeepCleanup);
  fMain->SetWindowName("CAEN Bias PS Control");
  fMain->MapSubwindows();
  fMain->Resize(fMain->GetDefaultSize());
  fMain->MapWindow();
  fMain->SetIconPixmap("./test.png");
  fMain->MapRaised();
  fMain->SetWMSizeHints(w, h, w, h, 0, 0);
}

void MyMainFrame::DoPing()
{
  TString ping = "echo \"" + std::string("\\") + "?ping\" | nc -w 3 " + std::string(fIP->GetText()) + " " + std::to_string((int)fPORT->GetNumber());
  if (gSystem->GetFromPipe(ping) == "PONG")
  {
    fStatusBar->AddLine("\n");
    std::time_t result = std::time(nullptr);
    fStatusBar->AddLine(std::asctime(std::localtime(&result)));
    fStatusBar->AddLine("Arduino http server is running");
  }
  else
  {
    fStatusBar->AddLine("\n");
    std::time_t result = std::time(nullptr);
    fStatusBar->AddLine(std::asctime(std::localtime(&result)));
    fStatusBar->AddLine("Can't connect to Arduino");
  }
}

void MyMainFrame::DoStatus()
{
  TString ping = "echo \"" + std::string("\\") + "?ping\" | nc -w 3 " + std::string(fIP->GetText()) + " " + std::to_string((int)fPORT->GetNumber());
  TString status = "echo \"" + std::string("\\") + "?status\" | nc -w 3 " + fIP->GetText() + " " + fPORT->GetNumber();
  TString voltage = "echo \"" + std::string("\\") + "?voltage\" | nc -w 3 " + fIP->GetText() + " " + fPORT->GetNumber();
  TString current = "echo \"" + std::string("\\") + "?current\" | nc -w 3 " + fIP->GetText() + " " + fPORT->GetNumber();

  if (gSystem->GetFromPipe(ping) == "PONG")
  {
    fStatusBar->AddLine("\n");
    std::time_t result = std::time(nullptr);
    fStatusBar->AddLine(std::asctime(std::localtime(&result)));
    fStatusBar->AddLine("Searching for connected PS units ...");


    TString status_out = gSystem->GetFromPipe(status);
    TString tok;
    Ssiz_t from = 0;
    while (status_out.Tokenize(tok, from, "[@]"))
      {
      fStatusBar->AddLine(tok);
      }
      fStatusBar->AddLine("\n");

    TString voltage_out = gSystem->GetFromPipe(voltage);
    tok.Clear();
    from = 0;
    while (voltage_out.Tokenize(tok, from, "[@]"))
      {
	fStatusBar->AddLine(tok);
      }
    fStatusBar->AddLine("\n");

    TString current_out = gSystem->GetFromPipe(current);
    tok.Clear();
    from=0;
    while (current_out.Tokenize(tok, from, "[@]"))
      {
	fStatusBar->AddLine(tok);
      }
    fStatusBar->AddLine("\n");

    fStatusBar->ShowBottom();
  }
  else
  {
    fStatusBar->AddLine("\n");
    std::time_t result = std::time(nullptr);
    fStatusBar->AddLine(std::asctime(std::localtime(&result)));
    fStatusBar->AddLine("Can't connect to Arduino");
  }
}

void MyMainFrame::DoBiasON()
{
  TString ping = "echo \"" + std::string("\\") + "?ping\" | nc -w 3 " + std::string(fIP->GetText()) + " " + std::to_string((int)fPORT->GetNumber());
  TString bias_on = "echo \"" + std::string("\\") + "?biason\" | nc -w 3 " + fIP->GetText() + " " + fPORT->GetNumber();

  if (gSystem->GetFromPipe(ping) == "PONG")
  {
    fStatusBar->AddLine("\n");
    std::time_t result = std::time(nullptr);
    fStatusBar->AddLine(std::asctime(std::localtime(&result)));
    fStatusBar->AddLine(gSystem->GetFromPipe(bias_on));
    fStatusBar->ShowBottom();
  }
  else
  {
    fStatusBar->AddLine("\n");
    std::time_t result = std::time(nullptr);
    fStatusBar->AddLine(std::asctime(std::localtime(&result)));
    fStatusBar->AddLine("Can't connect to Arduino");
  }
}

void MyMainFrame::DoBiasOFF()
{
  TString ping = "echo \"" + std::string("\\") + "?ping\" | nc -w 3 " + std::string(fIP->GetText()) + " " + std::to_string((int)fPORT->GetNumber());
  TString bias_off = "echo \"" + std::string("\\") + "?biasoff\" | nc -w 3 " + fIP->GetText() + " " + fPORT->GetNumber();

  if (gSystem->GetFromPipe(ping) == "PONG")
  {
    fStatusBar->AddLine("\n");
    std::time_t result = std::time(nullptr);
    fStatusBar->AddLine(std::asctime(std::localtime(&result)));
    fStatusBar->AddLine(gSystem->GetFromPipe(bias_off));
    fStatusBar->ShowBottom();
  }
  else
  {
    fStatusBar->AddLine("\n");
    std::time_t result = std::time(nullptr);
    fStatusBar->AddLine(std::asctime(std::localtime(&result)));
    fStatusBar->AddLine("Can't connect to Arduino");
  }
}

void MyMainFrame::DoBiasUPall()
{
  TString ping = "echo \"" + std::string("\\") + "?ping\" | nc -w 3 " + std::string(fIP->GetText()) + " " + std::to_string((int)fPORT->GetNumber());
  TString bias_up = "echo \"" + std::string("\\") + "?biasup\" | nc -w 3 " + fIP->GetText() + " " + fPORT->GetNumber();

  if (gSystem->GetFromPipe(ping) == "PONG")
  {
    fStatusBar->AddLine("\n");
    std::time_t result = std::time(nullptr);
    fStatusBar->AddLine(std::asctime(std::localtime(&result)));
    fStatusBar->AddLine(gSystem->GetFromPipe(bias_up));
    fStatusBar->ShowBottom();
  }
  else
  {
    fStatusBar->AddLine("\n");
    std::time_t result = std::time(nullptr);
    fStatusBar->AddLine(std::asctime(std::localtime(&result)));
    fStatusBar->AddLine("Can't connect to Arduino");
  }
}

void MyMainFrame::DoBiasDWNall()
{
  TString ping = "echo \"" + std::string("\\") + "?ping\" | nc -w 3 " + std::string(fIP->GetText()) + " " + std::to_string((int)fPORT->GetNumber());
  TString bias_dwn = "echo \"" + std::string("\\") + "?biasdown\" | nc -w 3 " + fIP->GetText() + " " + fPORT->GetNumber();

  if (gSystem->GetFromPipe(ping) == "PONG")
  {
    fStatusBar->AddLine("\n");
    std::time_t result = std::time(nullptr);
    fStatusBar->AddLine(std::asctime(std::localtime(&result)));
    fStatusBar->AddLine(gSystem->GetFromPipe(bias_dwn));
    fStatusBar->ShowBottom();
  }
  else
  {
    fStatusBar->AddLine("\n");
    std::time_t result = std::time(nullptr);
    fStatusBar->AddLine(std::asctime(std::localtime(&result)));
    fStatusBar->AddLine("Can't connect to Arduino");
  }
}

void MyMainFrame::DoSetBias()
{
  TString ping = "echo \"" + std::string("\\") + "?ping\" | nc -w 3 " + std::string(fIP->GetText()) + " " + std::to_string((int)fPORT->GetNumber());
  TString set_bias = "echo \"" + std::string("\\") + "?setbias_id_" + std::to_string((int)fID->GetNumber()) + "_volt_" + std::to_string((int)fVOLT->GetNumber()) + "\" | nc -w 3 " + fIP->GetText() + " " + fPORT->GetNumber();

  fStatusBar->AddLine(gSystem->GetFromPipe(set_bias));

  if (gSystem->GetFromPipe(ping) == "PONG")
  {
    fStatusBar->AddLine("\n");
    std::time_t result = std::time(nullptr);
    fStatusBar->AddLine(std::asctime(std::localtime(&result)));
    fStatusBar->AddLine(gSystem->GetFromPipe(set_bias));
    fStatusBar->ShowBottom();
  }
  else
  {
    fStatusBar->AddLine("\n");
    std::time_t result = std::time(nullptr);
    fStatusBar->AddLine(std::asctime(std::localtime(&result)));
    fStatusBar->AddLine("Can't connect to Arduino");
  }
}

void MyMainFrame::DoClear()
{
  fStatusBar->Clear();
}

void MyMainFrame::DoClose()
{
  //
  Int_t buttons = kMBYes + kMBNo;
  Int_t retval;

  new TGMsgBox(gClient->GetRoot(), fMain,
               "Exit", "Are you sure you want to exit?",
               kMBIconQuestion, buttons, &retval);
  if (retval == kMBYes)
  {
    gApplication->Terminate(0);
  }
}

MyMainFrame::~MyMainFrame()
{
  // Clean up used widgets: frames, buttons, layout hints
  fMain->Cleanup();
  delete fMain;
}

void biascontrol()
{
  // Popup the GUI...
  new MyMainFrame(gClient->GetRoot(), 1000, 200);
}

int main(int argc, char **argv)
{
  TApplication theApp("App", &argc, argv);
  biascontrol();
  theApp.Run();
  return 0;
}
