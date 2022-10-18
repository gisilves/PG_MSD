#include "TFile.h"
#include "TChain.h"
#include "TGraph.h"
#include "TLine.h"
#include "TROOT.h"
#include "TH1.h"
#include "TGTab.h"
#include "TApplication.h"
#include "TSystem.h"
#include "TThread.h"

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

template <typename T>
std::vector<T> reorder(std::vector<T> const &v)
{
  std::vector<T> reordered_vec(v.size());
  int j = 0;
  constexpr int order[] = {1, 0, 3, 2, 5, 4, 7, 6, 9, 8};
  for (int ch = 0; ch < 128; ch++)
  {
    for (int adc : order)
    {
      reordered_vec.at(adc * 128 + ch) = v.at(j);
      j++;
    }
  }
  return reordered_vec;
}

const char symb[] = {'|', '/', '-', '\\', 0};

MyMainFrame::MyMainFrame(const TGWindow *p, UInt_t w, UInt_t h)
{
  gROOT->ProcessLine("gErrorIgnoreLevel = 2022;");
  // On-line monitor with UDP server
  omServer = new udpServer(kUdpAddr, kUdpPort);
  // create the thread for the job
  th1 = new TThread("th1", JobThread, this);
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

  tf = fTab->AddTab("Realtime data");
  fHor_OM_Buttons = new TGHorizontalFrame(tf, 1280, 20);

  fOpenCalib = new TGTextButton(fHor_OM_Buttons, "&Open Calib");
  fOpenCalib->Connect("Clicked()", "MyMainFrame", this, "DoOpenCalibOnly()");
  fHor_OM_Buttons->AddFrame(fOpenCalib, new TGLayoutHints(kLHintsCenterX, 5, 5, 3, 4));

  fHor_Numbers_OM = new TGHorizontalFrame(tf, 1280, 20);

  boardsLabel = new TGLabel(fHor_Numbers_OM, "Board number:");
  fHor_Numbers_OM->AddFrame(boardsLabel, new TGLayoutHints(kLHintsLeft | kLHintsCenterY, 5, 2, 2, 2));
  fNumber2 = new TGNumberEntry(fHor_Numbers_OM, 0, 10, -1, TGNumberFormat::kNESReal, TGNumberFormat::kNEANonNegative, TGNumberFormat::kNELLimitMax, 0, 8);
  fHor_Numbers_OM->AddFrame(fNumber2, new TGLayoutHints(kLHintsCenterX | kLHintsCenterY, 5, 5, 5, 5));

  detectorLabel2 = new TGLabel(fHor_Numbers_OM, "Detector number:");
  fHor_Numbers_OM->AddFrame(detectorLabel2, new TGLayoutHints(kLHintsLeft | kLHintsCenterY, 5, 2, 2, 2));
  fNumber3 = new TGNumberEntry(fHor_Numbers_OM, 0, 10, -1, TGNumberFormat::kNESReal, TGNumberFormat::kNEANonNegative, TGNumberFormat::kNELLimitMax, 0, 1);
  fNumber3->GetNumberEntry()->Connect("ReturnPressed()", "MyMainFrame", this, "DoDraw()");
  fHor_Numbers_OM->AddFrame(fNumber3, new TGLayoutHints(kLHintsCenterX | kLHintsCenterY, 5, 5, 5, 5));

  fHor_Pedestal_OM = new TGHorizontalFrame(tf, 1280, 20);

  fPed2 = new TGCheckButton(fHor_Pedestal_OM, "Pedestal subtraction");
  fHor_Pedestal_OM->AddFrame(fPed2, new TGLayoutHints(kLHintsLeft | kLHintsCenterY, 5, 2, 2, 2));

  fShowAll = new TGCheckButton(fHor_Pedestal_OM, "Show both detectors on the board");
  fHor_Pedestal_OM->AddFrame(fShowAll, new TGLayoutHints(kLHintsRight | kLHintsCenterY, 5, 2, 2, 2));

  fStart = new TGTextButton(fHor_OM_Buttons, "&Start");
  fStart->SetToolTipText("Start online monitoring display");
  fStart->Connect("Clicked()", "MyMainFrame", this, "DoStart()");
  fHor_OM_Buttons->AddFrame(fStart, new TGLayoutHints(kLHintsCenterX, 5, 5, 3, 4));
  fStop = new TGTextButton(fHor_OM_Buttons, "&Stop");
  fStop->SetToolTipText("Stop online monitoring display");
  fStop->Connect("Clicked()", "MyMainFrame", this, "DoStop()");
  fHor_OM_Buttons->AddFrame(fStop, new TGLayoutHints(kLHintsCenterX, 5, 5, 3, 4));

  fExit2 = new TGTextButton(fHor_OM_Buttons, "&Exit");
  fExit2->Connect("Clicked()", "MyMainFrame", this, "DoClose()");
  fHor_OM_Buttons->AddFrame(fExit2, new TGLayoutHints(kLHintsCenterX, 5, 5, 3, 4));

  fHor_Status_OM = new TGHorizontalFrame(tf, 1024, 20);

  fStatusBar2 = new TGTextView(fHor_Status_OM, 500, 25);
  fStatusBar2->LoadBuffer("Online monitoring data plotting via UDP");

  fHor_Status_OM->AddFrame(fStatusBar2, new TGLayoutHints(kLHintsExpandX | kLHintsCenterX, 5, 5, 5, 5));

  tf->AddFrame(fHor_OM_Buttons, new TGLayoutHints(kLHintsExpandY | kLHintsCenterX, 2, 2, 5, 1));
  tf->AddFrame(fHor_Numbers_OM, new TGLayoutHints(kLHintsExpandY | kLHintsCenterX, 2, 2, 5, 1));
  tf->AddFrame(fHor_Pedestal_OM, new TGLayoutHints(kLHintsExpandX | kLHintsCenterX, 2, 2, 5, 1));
  tf->AddFrame(fHor_Status_OM, new TGLayoutHints(kLHintsExpandX | kLHintsCenterX, 2, 2, 5, 1));

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
  TChain *chain = new TChain("raw_events");
  TChain *chain2 = new TChain("raw_events_B");
  TChain *chain3 = new TChain("raw_events_C");
  TChain *chain4 = new TChain("raw_events_D");
  TChain *chain5 = new TChain("raw_events_E");
  TChain *chain6 = new TChain("raw_events_F");
  TChain *chain7 = new TChain("raw_events_G");
  TChain *chain8 = new TChain("raw_events_H");
  TChain *chain9 = new TChain("raw_events_I");
  TChain *chain10 = new TChain("raw_events_J");
  TChain *chain11 = new TChain("raw_events_K");
  TChain *chain12 = new TChain("raw_events_L");
  TChain *chain13 = new TChain("raw_events_M");
  TChain *chain14 = new TChain("raw_events_N");
  TChain *chain15 = new TChain("raw_events_O");
  TChain *chain16 = new TChain("raw_events_P");

  chain->Add(filename);
  if (newDAQ)
  {
    chain2->Add(filename);
    chain->AddFriend(chain2);

    if (boards >= 2)
    {
      chain3->Add(filename);
      chain4->Add(filename);
      chain->AddFriend(chain3);
      chain->AddFriend(chain4);
    }

    if (boards >= 3)
    {
      chain5->Add(filename);
      chain6->Add(filename);
      chain->AddFriend(chain5);
      chain->AddFriend(chain6);
    }

    if (boards >= 4)
    {
      chain7->Add(filename);
      chain8->Add(filename);
      chain->AddFriend(chain7);
      chain->AddFriend(chain8);
    }

    if (boards >= 5)
    {
      chain9->Add(filename);
      chain10->Add(filename);
      chain->AddFriend(chain9);
      chain->AddFriend(chain10);
    }

    if (boards >= 6)
    {
      chain11->Add(filename);
      chain12->Add(filename);
      chain->AddFriend(chain11);
      chain->AddFriend(chain12);
    }

    if (boards >= 7)
    {
      chain13->Add(filename);
      chain14->Add(filename);
      chain->AddFriend(chain13);
      chain->AddFriend(chain14);
    }

    if (boards >= 8)
    {
      chain15->Add(filename);
      chain16->Add(filename);
      chain->AddFriend(chain15);
      chain->AddFriend(chain16);
    }
  }
  Long64_t entries = chain->GetEntries();

  fStatusBar->AddLine("");
  fStatusBar->AddLine("Event: " + TGString(evt) + " of: " + TGString(entries - 1) + " for detector: " + TGString(detector));
  fStatusBar->ShowBottom();

  std::string alphabet = "ABCDEFGHIJKLMNOPQRSTWXYZ";
  // Read raw event from input chain TTree
  std::vector<unsigned int> *raw_event = 0;
  TBranch *RAW = 0;
  int is_branch_valid = 0;  

  if (detector%2)
  {
    chain->SetBranchAddress("RAW Event J7", &raw_event, &RAW);
  }
  else
  {
    chain->SetBranchAddress("RAW Event J5", &raw_event, &RAW);
  }

  chain->GetEntry(0);

  gr_event->SetMarkerColor(kRed + 1);
  gr_event->SetLineColor(kRed + 1);
  gr_event->SetMarkerStyle(23);
  gr_event->GetXaxis()->SetNdivisions(-raw_event->size() / 64, false);

  fStatusBar->AddLine("Opened raw file");

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
  delete chain;
  delete chain2;
}

void MyMainFrame::DoDrawOM(int evtnum, int detector, char calibfile[200], std::vector<uint32_t> evt)
{
  fStatusBar2->Clear();
  fStatusBar2->LoadBuffer("Online monitoring is running\t Reading event: " + TGString(evtnum) +
                          "\tRead " + TGString(evt.size()) + " channels for detector: " + TGString(detector) +
                          "\tCalibration file " + TGString(calib_open ? "loaded" : "not loaded"));

  gr_event->SetName("Event " + TGString(evtnum) + " Detector " + TGString(detector));
  gr_event->SetTitle("Event number " + TString::Format("%0d", (int)evtnum) + " Detector: " + TString::Format("%0d", (int)detector));
  gr_event->GetXaxis()->SetTitle("Strip number");
  gr_event->GetYaxis()->SetTitle("ADC");
  gr_event->GetYaxis()->SetTitleOffset(1.5);
  gr_event->GetXaxis()->SetTitleFont(62);
  gr_event->GetYaxis()->SetTitleFont(62);

  gr_event->SetMarkerColor(kRed + 1);
  gr_event->SetLineColor(kRed + 1);
  gr_event->SetMarkerStyle(23);
  gr_event->SetMarkerSize(0.5);

  int maxadc = -999;
  int minadc = 0;

  gr_event->Set(0);
  double signal;

  for (int chan = 0; chan < evt.size(); chan++)
  {
    if (fPed2->IsOn() && calib_open)
    {
      signal = evt[chan] - calib_data[detector].ped[chan];
    }
    else
    {
      signal = evt[chan];
    }

    if (signal > maxadc)
      maxadc = signal;
    if (signal < minadc)
      minadc = signal;

    gr_event->SetPoint(gr_event->GetN(), chan, signal);
  }
  gr_event->GetXaxis()->SetNdivisions((evt.size() - 1) / 64, false);
  gr_event->GetXaxis()->SetRangeUser(0, evt.size() - 1);
  gr_event->Draw("*lSAME");
  gr_event->Draw();
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

void MyMainFrame::DoStart()
{
  fEcanvas->GetCanvas()->Clear();
  fEcanvas->GetCanvas()->SetFrameLineColor(kBlack);
  // if the thread has been created and is not running, start it
  if (th1 && th1->GetState() != TThread::kRunningState)
    th1->Run();
  fStart->SetState(kButtonDisabled);
}

void MyMainFrame::DoStop()
{
  // if the thread has been created and is running, kill it
  if (th1 && th1->GetState() == TThread::kRunningState)
    th1->Join();
  fStatusBar2->LoadBuffer("Online monitoring stopped");
  fStart->SetState(kButtonUp);
}

void MyMainFrame::DoGetUDP()
{
  uint32_t header;
  omServer->Rx(&header, sizeof(header));
  // std::cout << "header: " << std::hex << header << std::endl;
  if (header != 0xfa4af1ca)
  {
    std::cout << "ERROR: header is not correct, skipping packet" << std::endl;
    return;
  }

  std::vector<uint32_t> evt(650);
  omServer->Rx(evt.data(), 2600);

  std::vector<uint32_t> evt_buffer;

  for (size_t i = 0; i < evt.size() - 10; i++)
  {
    evt_buffer.push_back((evt.at(i + 9) % (0x10000)) / 4);
    evt_buffer.push_back(((evt.at(i + 9) >> 16) % (0x10000)) / 4);
  }

  evt_buffer = reorder(evt_buffer);
  std::vector<uint32_t> detJ5 = std::vector<uint32_t>(evt_buffer.begin(), evt_buffer.begin() + evt_buffer.size() / 2);
  std::vector<uint32_t> detJ7 = std::vector<uint32_t>(evt_buffer.begin() + evt_buffer.size() / 2, evt_buffer.end());

  if (fShowAll->IsOn())
  {
    fNumber2->SetState(kTRUE);
    fNumber3->SetState(kFALSE);
    fEcanvas->GetCanvas()->cd();
    fEcanvas->GetCanvas()->Clear();
    fEcanvas->GetCanvas()->Divide(2, 1);

    fEcanvas->GetCanvas()->cd(1);
    DoDrawOM(evt[3], evt[4], (char *)(calibLabel->GetText())->GetString(), detJ5);
    gPad->SetGrid();
    gPad->Modified();
    gPad->Update();

    fEcanvas->GetCanvas()->cd(2);
    DoDrawOM(evt[3], evt[4] + 1, (char *)(calibLabel->GetText())->GetString(), detJ7);
    gPad->SetGrid();
    gPad->Modified();
    gPad->Update();
  }
  else
  {
    fNumber2->SetState(kFALSE);
    fNumber3->SetState(kTRUE);
    fEcanvas->GetCanvas()->cd();
    if (fNumber3->GetNumberEntry()->GetIntNumber())
    {
      DoDrawOM(evt[3], evt[4] + 1, (char *)(calibLabel->GetText())->GetString(), detJ7);
    }
    else
    {
      DoDrawOM(evt[3], evt[4], (char *)(calibLabel->GetText())->GetString(), detJ5);
    }
    gPad->SetGrid();
    gPad->Modified();
    gPad->Update();
  }
}

void *MyMainFrame::JobThread(void *arg)
{
  TThread::SetCancelOn(); // to allow to terminate (kill) the thread
  MyMainFrame *fMain = (MyMainFrame *)arg;
  int i = 0;
  while (1)
  {
    TThread::Sleep(0, 1e7);
    fMain->DoGetUDP();
    i++;
  }
  return 0;
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
