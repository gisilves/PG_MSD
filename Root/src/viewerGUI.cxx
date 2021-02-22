#include "TFile.h"
#include "TChain.h"
#include "TGraph.h"
#include "TLine.h"
#include "TROOT.h"
#include <TApplication.h>
#include <TCanvas.h>
#include <TString.h>
#include <TGCanvas.h>
#include <TGWindow.h>
#include <TGFrame.h>
#include <TRootEmbeddedCanvas.h>
#include <TGNumberEntry.h>
#include <TGLabel.h>
#include <TGTextView.h>
#include <TGLayout.h>
#include <TGFrame.h>
#include <TGFileDialog.h>
#include <TGClient.h>
#include "TH1.h"

#include "viewerGUI.hh"
#include "event.h"

#include <iostream>
#include <fstream>

MyMainFrame::MyMainFrame(const TGWindow *p, UInt_t w, UInt_t h)
{
  newDAQ = false;
  // Create a main frame
  fMain = new TGMainFrame(p, w, h);

  // Create canvas widget
  fEcanvas = new TRootEmbeddedCanvas("Ecanvas", fMain, 1024, 500);
  fMain->AddFrame(fEcanvas, new TGLayoutHints(kLHintsExpandX | kLHintsExpandY, 10, 10, 10, 1));

  fHor0 = new TGHorizontalFrame(fMain, 1024, 20);
  fHor0b = new TGHorizontalFrame(fMain, 1024, 20);

  TGVerticalFrame *fVer0 = new TGVerticalFrame(fHor0b, 10, 10);
  TGVerticalFrame *fVer1 = new TGVerticalFrame(fHor0b, 10, 10);

  fStatusBar = new TGTextView(fVer1, 500, 150);
  fStatusBar->LoadBuffer("Event viewer for DAMPE/FOOT raw data .root files.");
  fStatusBar->AddLine("");
  fStatusBar->AddLine("Root files must have been processed by miniTRB_compress or FOOT_compress utilities");
  fStatusBar->AddLine("");
  fStatusBar->AddLine("Files must have been acquired in raw (non compressed) mode.");
  fStatusBar->AddLine("");

  fHor1 = new TGHorizontalFrame(fVer0, 1024, 20);
  fHor3 = new TGHorizontalFrame(fMain, 1024, 20);
  fHor4 = new TGHorizontalFrame(fVer0, 1024, 20);

  fOpen = new TGTextButton(fHor0, "&Open");
  fOpen->Connect("Clicked()", "MyMainFrame", this, "DoOpen()");

  fOpenCal = new TGTextButton(fHor0, "&OpenCal");
  fOpenCal->Connect("Clicked()", "MyMainFrame", this, "DoOpenCalib()");

  fDraw = new TGTextButton(fHor0, "&Draw");
  fDraw->Connect("Clicked()", "MyMainFrame", this, "DoDraw()");

  fExit = new TGTextButton(fHor0, "&Exit", "gApplication->Terminate(0)");

  evtLabel = new TGLabel(fHor1, "Event Number:");
  fHor1->AddFrame(evtLabel, new TGLayoutHints(kLHintsLeft | kLHintsCenterY, 5, 2, 2, 2));
  fNumber = new TGNumberEntry(fHor1, 0, 10, -1, TGNumberFormat::kNESReal, TGNumberFormat::kNEANonNegative, TGNumberFormat::kNELNoLimits, 0, 1);
  fNumber->GetNumberEntry()->Connect("ReturnPressed()", "MyMainFrame", this, "DoDraw()");
  fHor1->AddFrame(fNumber, new TGLayoutHints(kLHintsCenterX, 5, 5, 5, 5));

  sideLabel = new TGLabel(fHor1, "Sensor number:");
  fHor1->AddFrame(sideLabel, new TGLayoutHints(kLHintsLeft | kLHintsCenterY, 5, 2, 2, 2));
  fNumber1 = new TGNumberEntry(fHor1, 0, 10, -1, TGNumberFormat::kNESReal, TGNumberFormat::kNEANonNegative, TGNumberFormat::kNELLimitMax, 0, 1);
  fNumber1->GetNumberEntry()->Connect("ReturnPressed()", "MyMainFrame", this, "DoDraw()");
  fHor1->AddFrame(fNumber1, new TGLayoutHints(kLHintsCenterX, 5, 5, 5, 5));

  fPed = new TGCheckButton(fHor4, "Pedestal subtraction");
  //fPed->SetOn();
  fHor4->AddFrame(fPed, new TGLayoutHints(kLHintsExpandX | kLHintsLeft | kLHintsCenterY, 5, 2, 2, 2));

  fileLabel = new TGLabel(fHor3, "No rootfile opened");
  calibLabel = new TGLabel(fHor3, "No calibfile opened");

  fHor3->AddFrame(fileLabel, new TGLayoutHints(kLHintsExpandX | kLHintsLeft | kLHintsCenterY, 5, 2, 2, 2));
  fHor3->AddFrame(calibLabel, new TGLayoutHints(kLHintsExpandX | kLHintsLeft | kLHintsCenterY, 5, 2, 2, 2));

  fHor4->AddFrame(fPed, new TGLayoutHints(kLHintsCenterX, 5, 5, 5, 5));

  fHor0->AddFrame(fOpen, new TGLayoutHints(kLHintsCenterX, 5, 5, 3, 4));
  fHor0->AddFrame(fOpenCal, new TGLayoutHints(kLHintsCenterX, 5, 5, 3, 4));
  fHor0->AddFrame(fDraw, new TGLayoutHints(kLHintsCenterX, 5, 5, 3, 4));
  fHor0->AddFrame(fExit, new TGLayoutHints(kLHintsCenterX, 5, 5, 3, 4));

  fHor0b->AddFrame(fVer0, new TGLayoutHints(kLHintsCenterX | kLHintsExpandX | kLHintsExpandY, 2, 2, 5, 1));
  fHor0b->AddFrame(fVer1, new TGLayoutHints(kLHintsCenterX | kLHintsExpandX | kLHintsExpandY, 2, 2, 5, 1));

  fMain->AddFrame(fHor0, new TGLayoutHints(kLHintsCenterX, 2, 2, 5, 1));
  fMain->AddFrame(fHor0b, new TGLayoutHints(kLHintsExpandX | kLHintsCenterX, 2, 2, 5, 1));

  fVer0->AddFrame(fHor1, new TGLayoutHints(kLHintsCenterX | kLHintsCenterY, 2, 2, 5, 1));
  fVer0->AddFrame(fHor4, new TGLayoutHints(kLHintsCenterX | kLHintsCenterY, 2, 2, 5, 1));
  fMain->AddFrame(fHor3, new TGLayoutHints(kLHintsExpandX | kLHintsCenterX, 2, 2, 5, 1));

  fVer1->AddFrame(fStatusBar, new TGLayoutHints(kLHintsCenterX | kLHintsBottom | kLHintsLeft | kLHintsExpandY, 5, 5, 2, 2));

  fMain->SetCleanup(kDeepCleanup);
  fMain->SetWindowName("DAMPE/FOOT Raw Event Viewer");
  fMain->MapSubwindows();
  fMain->Resize(fMain->GetDefaultSize());
  fMain->MapWindow();
}

const char *filetypesROOT[] = {
    "ROOT files", "*.root",
    "All files", "*",
    0, 0};

const char *filetypesCalib[] = {
    "Calib files", "*.cal",
    "All files", "*",
    0, 0};

void MyMainFrame::viewer(int evt, int side, char filename[200], char calibfile[200])
{
  TChain *chain = new TChain("raw_events");
  chain->Add(filename);
  if (newDAQ)
  {
    TChain *chain2 = new TChain("raw_events_B");
    chain2->Add(filename);
    chain->AddFriend(chain2);
  }

  Long64_t entries = chain->GetEntries();

  fStatusBar->AddLine("");
  fStatusBar->AddLine("Event: " + TGString(evt) + " of: " + TGString(entries) + " for side: " + TGString(side));
  fStatusBar->ShowBottom();

  //Read Calibration file
  calib cal;
  read_calib(calibfile, &cal);

  // Read raw event from input chain TTree
  std::vector<unsigned int> *raw_event = 0;
  TBranch *RAW = 0;

  chain->SetBranchAddress("RAW Event", &raw_event, &RAW);

  if (side == 1)
  {
    chain->SetBranchAddress("RAW Event B", &raw_event, &RAW);
  }

  gr_event->SetMarkerColor(kRed + 1);
  gr_event->SetLineColor(kRed + 1);

  gr_event->SetMarkerStyle(23);
  gr_event->GetXaxis()->SetNdivisions(raw_event->size() / 64, false);

  if (newDAQ)
  {
    fStatusBar->AddLine("Opened FOOT raw file from DE10nano");
  }
  else
  {
    fStatusBar->AddLine("Opened DaMPE/FOOT raw file from miniTRB");
  }

  int maxadc = -999;
  int minadc = 0;

  chain->GetEntry(evt);

  gr_event->Set(0);

  for (int chan = 0; chan < raw_event->size(); chan++)
  {
    double ADC = raw_event->at(chan);
    double signal;

    if (fPed->IsOn())
    {
      signal = ADC - cal.ped[chan];
    }
    else
    {
      signal = ADC;
    }

    if (signal > maxadc)
      maxadc = signal;
    if (signal < minadc)
      minadc = signal;

    gr_event->SetPoint(gr_event->GetN(), chan, signal);
  }

  TH1F *frame = gPad->DrawFrame(0, minadc - 20, raw_event->size(), maxadc + 20);

  frame->SetTitle("Event number " + TString::Format("%0d", (int)evt) + " Side: " + TString::Format("%0d", (int)side));
  frame->GetXaxis()->SetNdivisions(-16);
  frame->GetXaxis()->SetTitle("Strip number");
  frame->GetYaxis()->SetTitle("ADC");

  gr_event->SetMarkerSize(0.5);
  gr_event->Draw("*lSAME");

  for (int iline = 0; iline < 15; iline++)
  {
    line[iline] = new TGraph(2);
    line[iline]->SetPoint(0, (iline + 1) * 64, minadc - 20);
    line[iline]->SetPoint(1, (iline + 1) * 64, maxadc + 20);
    line[iline]->SetLineColor(kGray + 2);
    line[iline]->Draw();
  }

  gr_event->Draw();

  TCanvas *fCanvas = fEcanvas->GetCanvas();
  fCanvas->cd();
  fCanvas->Update();

  delete chain;
}

void MyMainFrame::DoDraw()
{
  if (gROOT->GetListOfFiles()->FindObject((char *)(fileLabel->GetText())->GetString()) &&
      gROOT->GetListOfFiles()->FindObject((char *)(fileLabel->GetText())->GetString()))
  {
    viewer(fNumber->GetNumberEntry()->GetIntNumber(), fNumber1->GetNumberEntry()->GetIntNumber(), (char *)(fileLabel->GetText())->GetString(), (char *)(calibLabel->GetText())->GetString());
  }
}

void MyMainFrame::DoOpen()
{
  static TString dir(".");
  TGFileInfo fi;
  fi.fFileTypes = filetypesROOT;
  fi.fIniDir = StrDup(dir);
  new TGFileDialog(gClient->GetRoot(), fMain, kFDOpen, &fi);
  fNumber1->SetState(false);

  if (fi.fFilename)
  {
    TFile *f = TFile::Open(fi.fFilename);
    bool IStree = f->GetListOfKeys()->Contains("raw_events");

    bool ISfoot = f->GetListOfKeys()->Contains("raw_events_B");
    if (ISfoot)
    {
      fNumber1->SetState(true);
      newDAQ = true;
    }

    if (IStree)
    {
      TTree *t = (TTree *)f->Get("raw_events");
      int entries = t->GetEntries();

      fStatusBar->Clear();
      fileLabel->SetText(fi.fFilename);
      fNumber->SetText("0");
      DoOpenCalib();
    }
    else
    {
      fStatusBar->Clear();
      fStatusBar->AddLine("ERROR: the file does not contain raw events TTree");
      return;
    }
  }
  dir = fi.fIniDir;
}

void MyMainFrame::DoOpenCalib()
{
  static TString dir(".");
  TGFileInfo fi;
  fi.fFileTypes = filetypesCalib;
  fi.fIniDir = StrDup(dir);
  printf("fIniDir = %s\n", fi.fIniDir);
  new TGFileDialog(gClient->GetRoot(), fMain, kFDOpen, &fi);
  printf("Open file: %s (dir: %s)\n", fi.fFilename, fi.fIniDir);

  if (fi.fFilename)
  {
    calibLabel->SetText(fi.fFilename);
    fStatusBar->LoadBuffer("Run: " + TGString(fileLabel->GetText()->GetString()));
    fStatusBar->AddLine("");
    fStatusBar->AddLine("Calibration: " + TGString(calibLabel->GetText()->GetString()));
    DoDraw();
  }
  else
  {
    fStatusBar->Clear();
    fStatusBar->AddLine("ERROR: calibration file is empty");
    return;
  }

  dir = fi.fIniDir;
}

MyMainFrame::~MyMainFrame()
{
  // Clean up used widgets: frames, buttons, layout hints
  fMain->Cleanup();
  delete fMain;
}

void viewerGUI()
{
  // Popup the GUI...
  new MyMainFrame(gClient->GetRoot(), 500, 500);
}

int main(int argc, char **argv)
{
  TApplication theApp("App", &argc, argv);
  viewerGUI();
  theApp.Run();
  return 0;
}
