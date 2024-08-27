#include "TFile.h"
#include "TChain.h"
#include "TGraph.h"
#include "TLine.h"
#include "TROOT.h"
#include "TH1.h"
#include "TGTab.h"
#include "TApplication.h"
#include "TSystem.h"

#include <TCanvas.h>
#include <TColor.h>
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
#include <TGMsgBox.h>
#include <TGPicture.h>

#include "viewerGUI.h"

#include <iostream>
#include <fstream>
#include <string>

#define max_detectors 16

MyMainFrame::MyMainFrame(const TGWindow *p, UInt_t w, UInt_t h)
{
  gROOT->ProcessLine("gErrorIgnoreLevel = 2022;");
  gr_event = new TGraph();

  newDAQ = false;
  boards = 1;
  // Create a main frame
  fMain = new TGMainFrame(p, w, h);
  fMain->Connect("CloseWindow()", "MyMainFrame", this, "DoClose()");
  fMain->DontCallClose();

  //--------- create the Tab widget
  TGTab *fTab = new TGTab(fMain, 300, 300);
  TGLayoutHints *fL3 = new TGLayoutHints(kLHintsTop | kLHintsLeft, 5, 5, 5, 5);
  TGCompositeFrame *tf = fTab->AddTab("Data from file");

  // Create canvas widget
  fEcanvas = new TRootEmbeddedCanvas("Ecanvas", fMain, 1024, 800);
  fMain->AddFrame(fEcanvas, new TGLayoutHints(kLHintsExpandX | kLHintsExpandY, 10, 10, 10, 1));
  fMain->AddFrame(fTab, new TGLayoutHints(kLHintsExpandX, 2, 2, 5, 1));

  fHor_Buttons = new TGHorizontalFrame(tf, 1024, 20);
  fHor_Numbers = new TGHorizontalFrame(tf, 1024, 20);
  fHor_Pedestal = new TGHorizontalFrame(tf, 1024, 20);
  fHor_Status = new TGHorizontalFrame(tf, 1024, 20);

  fStatusBar = new TGTextView(fHor_Status, 500, 150);
  fStatusBar->LoadBuffer("Event viewer for microstrip raw data .root files.");
  fStatusBar->AddLine("");
  fStatusBar->AddLine("Files must have been acquired in raw (non compressed) mode.");
  fStatusBar->AddLine("");

  fHor_Status->AddFrame(fStatusBar, new TGLayoutHints(kLHintsExpandX | kLHintsCenterX, 5, 5, 5, 5));

  fHor_Files = new TGHorizontalFrame(fMain, 1024, 20);

  fOpen = new TGTextButton(fHor_Buttons, "&Open");
  fOpen->Connect("Clicked()", "MyMainFrame", this, "DoOpen()");

  fDraw = new TGTextButton(fHor_Buttons, "&Draw");
  fDraw->Connect("Clicked()", "MyMainFrame", this, "DoDraw()");

  // fExit = new TGTextButton(fHor_Buttons, "&Exit", "gApplication->Terminate(0)");
  fExit = new TGTextButton(fHor_Buttons, "&Exit");
  fExit->Connect("Clicked()", "MyMainFrame", this, "DoClose()");

  evtLabel = new TGLabel(fHor_Numbers, "Event Number:");
  fHor_Numbers->AddFrame(evtLabel, new TGLayoutHints(kLHintsLeft | kLHintsCenterY, 5, 2, 2, 2));
  fNumber = new TGNumberEntry(fHor_Numbers, 0, 10, -1, TGNumberFormat::kNESReal, TGNumberFormat::kNEANonNegative, TGNumberFormat::kNELLimitMax, 0, 1);
  fNumber->GetNumberEntry()->Connect("ReturnPressed()", "MyMainFrame", this, "DoDraw()");
  fHor_Numbers->AddFrame(fNumber, new TGLayoutHints(kLHintsCenterX, 5, 5, 5, 5));

  detectorLabel = new TGLabel(fHor_Numbers, "Detector number:");
  fHor_Numbers->AddFrame(detectorLabel, new TGLayoutHints(kLHintsLeft | kLHintsCenterY, 5, 2, 2, 2));
  fNumber1 = new TGNumberEntry(fHor_Numbers, 0, 10, -1, TGNumberFormat::kNESReal, TGNumberFormat::kNEANonNegative, TGNumberFormat::kNELLimitMax, 0, 6);
  fNumber1->GetNumberEntry()->Connect("ReturnPressed()", "MyMainFrame", this, "DoDraw()");
  fHor_Numbers->AddFrame(fNumber1, new TGLayoutHints(kLHintsCenterX, 5, 5, 5, 5));

  fPed = new TGCheckButton(fHor_Pedestal, "Pedestal subtraction");
  fHor_Pedestal->AddFrame(fPed, new TGLayoutHints(kLHintsExpandX | kLHintsLeft | kLHintsCenterY, 5, 2, 2, 2));

  fileLabel = new TGLabel(fHor_Files, "No rootfile opened");
  calibLabel = new TGLabel(fHor_Files, "No calibfile opened");

  fHor_Files->AddFrame(calibLabel, new TGLayoutHints(kLHintsExpandX | kLHintsLeft | kLHintsCenterY, 5, 2, 2, 2));
  fHor_Files->AddFrame(fileLabel, new TGLayoutHints(kLHintsExpandX | kLHintsLeft | kLHintsCenterY, 5, 2, 2, 2));

  fHor_Buttons->AddFrame(fOpen, new TGLayoutHints(kLHintsCenterX, 5, 5, 3, 4));
  fHor_Buttons->AddFrame(fDraw, new TGLayoutHints(kLHintsCenterX, 5, 5, 3, 4));
  fHor_Buttons->AddFrame(fExit, new TGLayoutHints(kLHintsCenterX, 5, 5, 3, 4));

  tf->AddFrame(fHor_Buttons, new TGLayoutHints(kLHintsCenterX, 2, 2, 5, 1));
  tf->AddFrame(fHor_Numbers, new TGLayoutHints(kLHintsCenterX, 2, 2, 5, 1));
  tf->AddFrame(fHor_Pedestal, new TGLayoutHints(kLHintsExpandX | kLHintsCenterX, 2, 2, 5, 1));
  tf->AddFrame(fHor_Status, new TGLayoutHints(kLHintsExpandX | kLHintsCenterX, 2, 2, 5, 1));

  fMain->AddFrame(fHor_Files, new TGLayoutHints(kLHintsExpandX | kLHintsCenterX, 2, 2, 5, 1));

  fMain->SetCleanup(kDeepCleanup);
  fMain->SetWindowName("Microstrip Raw Event Viewer");
  fMain->MapSubwindows();
  fMain->Resize(fMain->GetDefaultSize());
  fMain->MapWindow();

  fEcanvas->GetCanvas()->cd();
  double x_arr[77] = {215.73, 219.69, 224.68, 228.48, 233.54, 237.34, 236.52, 237.76, 239.23, 241.1, 242.4, 243.9, 246.2, 247.04, 250.21, 251.12, 253.07, 255.16,
                      255.82, 256.69, 259.05, 261.17, 260.93, 261.21, 262.03, 264.96, 269.82, 270.24, 276.35, 278.68, 279.1, 287.54, 287.96, 296.4, 296.82, 305.26,
                      305.68, 314.12, 314.54, 322.98, 323.4, 328.79, 331.14, 332.26, 334.17, 335.64, 337.32, 338.28, 339.06, 340.4, 339.64, 341.12, 346.18, 349.98,
                      355.04, 358.84, 363.9, 367.7, 372.76, 376.56, 381.62, 385.42, 390.48, 394.62, 399.34, 403.14, 408.2, 410.44, 416.52, 417.54, 420.85, 423.32,
                      423.66, 424.81, 424.68, 425.28, 425.47};
  double y_arr[77] = {434.64, 402.27, 455.17, 393.55, 460.92, 385.68, 172.06, 130.98, 216.79, 100.46, 471.23, 245.8, 379.93, 69.58, 271.81, 477.74, 44.57, 523.15,
                      375.05, 438.09, 297.44, 549.03, 408.59, 27.61, 358.92, 330.34, 576.98, 9.87, 485.86, 591.58, -4.11, 598.82, -14.52, 600.04, -22.12, 597.93,
                      -27.01, 590.53, -29.62, 576.15, -30.55, 342.74, 549.92, -29.39, 385.7, 524.8, 317.81, 416.4, 495.34, 462.78, 438.88, -26.91, 312.98, -23.01,
                      312.79, -17.36, 314.93, -9.33, 319.96, 1.57, 327.19, 15.95, 337.01, 34.82, 349.47, 54.65, 364.1, 84.69, 118.56, 369.98, 151.17, 183.57, 341.11,
                      212.44, 300.71, 271.46, 241.77};

  TGraph *splash = new TGraph(77, x_arr, y_arr);
  splash->SetTitle("");
  splash->SetMarkerStyle(39);
  splash->SetMarkerSize(2);
  splash->GetXaxis()->SetTickLength(0.);
  splash->GetYaxis()->SetTickLength(0.);
  splash->GetXaxis()->SetLabelSize(0.);
  splash->GetYaxis()->SetLabelSize(0.);
  splash->Draw("AP");
}

const char *filetypesROOT[] = {
    "ROOT files", "*.root",
    "All files", "*",
    0, 0};

const char *filetypesCalib[] = {
    "Calib files", "*.cal",
    "All files", "*",
    0, 0};

void MyMainFrame::viewer(int evt, int detector, char filename[200], char calibfile[200], int boards)
{
  std::string alphabet = "ABCDEFGHIJKLMNOPQRSTWXYZ";
  std::vector<TChain*> data_chains;
  TString chain_name;

  for (int i = 0; i < max_detectors; i++) 
  {
    if (i == 0)
    {
      chain_name = "raw_events";
    }
    else
    {
      chain_name = "raw_events_" + (TString) alphabet.at(i);
    }
    TChain* chain = new TChain(chain_name);
    data_chains.push_back(chain);
  }

  data_chains.at(0)->Add(filename);
  if (newDAQ)
  {
    data_chains.at(1)->Add(filename);
  }

  for (size_t detector = 2; detector < 2 * boards; detector += 2)
  {
    data_chains.at(detector)->Add(filename);
    data_chains.at(detector + 1)->Add(filename);
  } 

  Long64_t entries = data_chains.at(0)->GetEntries();
  fStatusBar->AddLine("");
  fStatusBar->AddLine("Event: " + TGString(evt) + " of: " + TGString(entries - 1) + " for detector: " + TGString(detector));
  fStatusBar->ShowBottom();

  // Read raw event from input chain TTree
  std::vector<unsigned int> *raw_event = 0;
  TBranch *RAW = 0;
  int is_branch_valid = 0;  

  TString branch_name;
  if (detector == 0)
  {
    branch_name = "RAW Event";
    data_chains.at(detector)->SetBranchAddress(branch_name, &raw_event, &RAW);
  }
  else
  {
    branch_name = "RAW Event " + (TString) alphabet.at(detector);
    data_chains.at(detector)->SetBranchAddress(branch_name, &raw_event, &RAW);
  }

  data_chains.at(detector)->GetEntry(0);

  gr_event->SetMarkerColor(kRed + 1);
  gr_event->SetLineColor(kRed + 1);
  gr_event->SetMarkerStyle(23);
  gr_event->GetXaxis()->SetNdivisions(-raw_event->size() / 64, false);

  fStatusBar->AddLine("Opened raw file");

  int maxadc = -999;
  int minadc = 0;

  data_chains.at(detector)->GetEntry(evt);

  gr_event->Set(0);

  for (int chan = 0; chan < raw_event->size(); chan++)
  {
    double ADC = raw_event->at(chan);
    double signal;

    if (fPed->IsOn())
    {
      signal = ADC - calib_data[detector].ped[chan];
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

  TH1F *frame = gPad->DrawFrame(0, minadc - 100, raw_event->size(), maxadc + 100);

  int nVAs = raw_event->size() / 64;

  frame->SetTitle("Event number " + TString::Format("%0d", (int)evt) + " Detector: " + TString::Format("%0d", (int)detector));
  frame->GetXaxis()->SetNdivisions(-nVAs);
  if (nVAs == 1)
  {
    frame->GetXaxis()->SetNdivisions(10);
  }

  frame->GetXaxis()->SetTitle("Strip number");
  frame->GetYaxis()->SetTitle("ADC");

  gr_event->SetMarkerSize(0.5);
  gr_event->Draw("*lSAME");
  gr_event->Draw();
  TCanvas *fCanvas = fEcanvas->GetCanvas();
  fCanvas->SetGrid();
  fCanvas->cd();
  fCanvas->Update();
}

void MyMainFrame::DoDraw()
{
  if (gROOT->GetListOfFiles()->FindObject((char *)(fileLabel->GetText())->GetString()))
  {
    viewer(fNumber->GetNumberEntry()->GetIntNumber(), fNumber1->GetNumberEntry()->GetIntNumber(), (char *)(fileLabel->GetText())->GetString(), (char *)(calibLabel->GetText())->GetString(), boards);
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
      fNumber1->SetLimitValues(0, 1);
      newDAQ = true;
      if (f->GetListOfKeys()->Contains("raw_events_C"))
      {
        fNumber1->SetLimitValues(0, 3);
        boards = 2;
      }
      if (f->GetListOfKeys()->Contains("raw_events_E"))
      {
        fNumber1->SetLimitValues(0, 5);
        boards = 3;
      }
      if (f->GetListOfKeys()->Contains("raw_events_G"))
      {
        fNumber1->SetLimitValues(0, 7);
        boards = 4;
      }
      if (f->GetListOfKeys()->Contains("raw_events_I"))
      {
        fNumber1->SetLimitValues(0, 9);
        boards = 5;
      }
      if (f->GetListOfKeys()->Contains("raw_events_K"))
      {
        fNumber1->SetLimitValues(0, 11);
        boards = 6;
      }
      if (f->GetListOfKeys()->Contains("raw_events_M"))
      {
        fNumber1->SetLimitValues(0, 13);
        boards = 7;
      }
      if (f->GetListOfKeys()->Contains("raw_events_O"))
      {
        fNumber1->SetLimitValues(0, 15);
        boards = 8;
      }
    }

    if (IStree)
    {
      TTree *t = (TTree *)f->Get("raw_events");
      int entries = t->GetEntries();
      fNumber->SetLimitValues(0, entries - 1);
      fStatusBar->Clear();
      fileLabel->SetText(fi.fFilename);
      fNumber->SetText("0");

      Int_t buttons = kMBYes + kMBNo;
      Int_t retval;

      new TGMsgBox(gClient->GetRoot(), fMain,
                   "Calib?", "Do you want to load the calibration file?",
                   kMBIconQuestion, buttons, &retval);
      if (retval == kMBYes)
      {
        fPed->SetOn();
        DoOpenCalib();
      }
      else
      {
        fPed->SetDisabledAndSelected(0);
      }
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

void MyMainFrame::DoOpenCalibOnly()
{
  DoOpenCalib();
}

void MyMainFrame::DoOpenCalib()
{
  static TString dir(".");
  TGFileInfo fi;
  fi.fFileTypes = filetypesCalib;
  fi.fIniDir = StrDup(dir);
  new TGFileDialog(gClient->GetRoot(), fMain, kFDOpen, &fi);

  if (fi.fFilename)
  {
    calibLabel->SetText(fi.fFilename);
    fStatusBar->LoadBuffer("Run: " + TGString(fileLabel->GetText()->GetString()));
    fStatusBar->AddLine("");
    fStatusBar->AddLine("Calibration: " + TGString(calibLabel->GetText()->GetString()));
    calib_open = true;
  }
  else
  {
    fStatusBar->AddLine("ERROR: calibration file is empty");
    return;
  }

  dir = fi.fIniDir;

  calib_data = read_calib_all((char *)(calibLabel->GetText())->GetString(), false);
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

void viewerGUI()
{
  // Popup the GUI...
  MyMainFrame *window = new MyMainFrame(gClient->GetRoot(), 1000, 1000);
}

int main(int argc, char **argv)
{
  TApplication theApp("App", &argc, argv);
  viewerGUI();
  theApp.Run();
  return 0;
}
