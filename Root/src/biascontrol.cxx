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
#include "biascontrol.hh"

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

  TGVerticalFrame *fVer0 = new TGVerticalFrame(fHor0b, 0.3*w, 10);
  TGVerticalFrame *fVer1 = new TGVerticalFrame(fHor0b, 0.7*w, 10);

  fStatusBar = new TGTextView(fVer1, 500, 150);
  std::time_t result = std::time(nullptr);
  fStatusBar->AddLine(std::asctime(std::localtime(&result)));
  fStatusBar->AddLine("Starting bias ps control ...");
  
  fHor1 = new TGHorizontalFrame(fVer0, 1024, 20);
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

  ipLabel = new TGLabel(fHor1, "Arduino IP:");
  fHor1->AddFrame(ipLabel, new TGLayoutHints(kLHintsLeft | kLHintsCenterY, 5, 2, 2, 2));
  fIP = new TGTextEntry(fHor1, "192.168.1.242");
  fHor1->AddFrame(fIP, new TGLayoutHints(kLHintsCenterX, 5, 5, 5, 5));

  portLabel = new TGLabel(fHor1, "Port:");
  fHor1->AddFrame(portLabel, new TGLayoutHints(kLHintsLeft | kLHintsCenterY, 5, 2, 2, 2));
  fPORT = new TGNumberEntry(fHor1, 80, 10, -1, TGNumberFormat::kNESInteger, TGNumberFormat::kNEANonNegative, TGNumberFormat::kNELNoLimits, 0, 1);
  fHor1->AddFrame(fPORT, new TGLayoutHints(kLHintsCenterX, 5, 5, 5, 5));

  fBiasON = new TGTextButton(fHor2, "&Bias ON");
  fBiasON->Connect("Clicked()", "MyMainFrame", this, "DoBiasON()");
  fHor2->AddFrame(fBiasON, new TGLayoutHints(kLHintsCenterX, 5, 5, 3, 4));

  fBiasOFF = new TGTextButton(fHor2, "&Bias OFF");
  fBiasOFF->Connect("Clicked()", "MyMainFrame", this, "DoBiasOFF()");
  fHor2->AddFrame(fBiasOFF, new TGLayoutHints(kLHintsCenterX, 5, 5, 3, 4));

  fBiasUP = new TGTextButton(fHor2, "&Bias UP");
  fBiasUP->Connect("Clicked()", "MyMainFrame", this, "DoBiasUP()");
  fHor2->AddFrame(fBiasUP, new TGLayoutHints(kLHintsCenterX, 5, 5, 3, 4));

  fBiasDWN = new TGTextButton(fHor2, "&Bias DWN");
  fBiasDWN->Connect("Clicked()", "MyMainFrame", this, "DoBiasDWN()");
  fHor2->AddFrame(fBiasDWN, new TGLayoutHints(kLHintsCenterX, 5, 5, 3, 4));

  fVer0->AddFrame(fHor1, new TGLayoutHints(kLHintsCenterX | kLHintsCenterY, 2, 2, 5, 1));
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
  if(gSystem->GetFromPipe(ping) == "PONG")
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

  if(gSystem->GetFromPipe(ping) == "PONG")
  {
    fStatusBar->AddLine("\n");
    std::time_t result = std::time(nullptr);
    fStatusBar->AddLine(std::asctime(std::localtime(&result)));
    fStatusBar->AddLine("Searching for connected PS units ...");
    fStatusBar->AddLine(gSystem->GetFromPipe(status));
    fStatusBar->AddLine("\n");
    fStatusBar->AddLine(gSystem->GetFromPipe(voltage));
    fStatusBar->AddLine("\n");
    fStatusBar->AddLine(gSystem->GetFromPipe(current));
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

  if(gSystem->GetFromPipe(ping) == "PONG")
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

  if(gSystem->GetFromPipe(ping) == "PONG")
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

void MyMainFrame::DoBiasUP()
{  
  TString ping = "echo \"" + std::string("\\") + "?ping\" | nc -w 3 " + std::string(fIP->GetText()) + " " + std::to_string((int)fPORT->GetNumber());
  TString bias_up = "echo \"" + std::string("\\") + "?biasup\" | nc -w 3 " + fIP->GetText() + " " + fPORT->GetNumber();

  if(gSystem->GetFromPipe(ping) == "PONG")
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

void MyMainFrame::DoBiasDWN()
{  
  TString ping = "echo \"" + std::string("\\") + "?ping\" | nc -w 3 " + std::string(fIP->GetText()) + " " + std::to_string((int)fPORT->GetNumber());
  TString bias_dwn = "echo \"" + std::string("\\") + "?biasdown\" | nc -w 3 " + fIP->GetText() + " " + fPORT->GetNumber();

  if(gSystem->GetFromPipe(ping) == "PONG")
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
  if(retval == kMBYes){    
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
