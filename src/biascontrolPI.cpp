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
#include "biascontrolPI.h"

#include "HV_manager.c"

#include <iostream>
#include <fstream>
#include <ctime>

#define I2C_PATH "/dev/i2c-1" //VERIFY IF I2C PATH IS CORRECT
#define I2C_ADD 0x70          //VERIFY IF I2C ADDRESS IS CORRECT
int iic_rasp;

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

  fHor1b = new TGHorizontalFrame(fVer0, 1024, 20);
  fHor2 = new TGHorizontalFrame(fVer0, 1024, 20);
  fHor3 = new TGHorizontalFrame(fVer0, 1024, 20);

  fStatus = new TGTextButton(fHor0, "&Status");
  fStatus->Connect("Clicked()", "MyMainFrame", this, "DoStatus()");
  fHor0->AddFrame(fStatus, new TGLayoutHints(kLHintsCenterX, 5, 5, 3, 4));

  fClear = new TGTextButton(fHor0, "&Clear");
  fClear->Connect("Clicked()", "MyMainFrame", this, "DoClear()");
  fHor0->AddFrame(fClear, new TGLayoutHints(kLHintsCenterX, 5, 5, 3, 4));

  fExit = new TGTextButton(fHor0, "&Exit");
  fExit->Connect("Clicked()", "MyMainFrame", this, "DoClose()");
  fHor0->AddFrame(fExit, new TGLayoutHints(kLHintsCenterX, 5, 5, 3, 4));

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

  fVer0->AddFrame(fHor1b, new TGLayoutHints(kLHintsCenterX | kLHintsCenterY, 2, 2, 5, 1));
  fVer0->AddFrame(fHor2, new TGLayoutHints(kLHintsCenterX | kLHintsCenterY, 2, 2, 5, 1));
  fVer0->AddFrame(fHor3, new TGLayoutHints(kLHintsExpandX | kLHintsCenterX, 2, 2, 5, 1));
  fVer1->AddFrame(fStatusBar, new TGLayoutHints(kLHintsCenterX | kLHintsBottom | kLHintsLeft | kLHintsExpandY, 5, 5, 2, 2));

  fHor0b->AddFrame(fVer0, new TGLayoutHints(kLHintsCenterX | kLHintsExpandX | kLHintsExpandY, 2, 2, 5, 1));
  fHor0b->AddFrame(fVer1, new TGLayoutHints(kLHintsCenterX | kLHintsExpandX | kLHintsExpandY, 2, 2, 5, 1));
  fMain->AddFrame(fHor0, new TGLayoutHints(kLHintsCenterX, 2, 2, 5, 1));
  fMain->AddFrame(fHor0b, new TGLayoutHints(kLHintsExpandX | kLHintsCenterX, 2, 2, 5, 1));

  fMain->SetCleanup(kDeepCleanup);
  fMain->SetWindowName("CAEN Bias PS Control for RaspberryPi");
  fMain->MapSubwindows();
  fMain->Resize(fMain->GetDefaultSize());
  fMain->MapWindow();
  fMain->SetIconPixmap("./test.png");
  fMain->MapRaised();
  fMain->SetWMSizeHints(w, h, w, h, 0, 0);
}

void MyMainFrame::DoStatus()
{
  fStatusBar->AddLine("\n");
  std::time_t result = std::time(nullptr);
  fStatusBar->AddLine(std::asctime(std::localtime(&result)));
  fStatusBar->AddLine("Searching for connected PS units ...");

  uint8_t serial = 0;

  fStatusBar->AddLine("\n");
  fStatusBar->AddLine(std::asctime(std::localtime(&result)));

  for (int i = 0; i < 16; i++)
  {
    if (A7585_GetSerialNumber(iic_rasp, I2C_ADD + i, serial) == (I2C_ADD + i))
    {
      fStatusBar->AddLine("Device ID " + (TString)serial + " connected");
    }
  }

  fStatusBar->ShowBottom();
}

void MyMainFrame::DoBiasON()
{
  fStatusBar->AddLine("\n");
  std::time_t result = std::time(nullptr);
  fStatusBar->AddLine(std::asctime(std::localtime(&result)));
  fStatusBar->AddLine("Setting Bias ON at 50V");
  fStatusBar->ShowBottom();

  uint8_t serial;
  for (int i = 0; i < 16; i++)
  {
    if (A7585_GetSerialNumber(iic_rasp, I2C_ADD + i, serial))
    {
      A7585_Set_Enable(iic_rasp, I2C_ADD + i, true);
      A7585_Set_V(iic_rasp, I2C_ADD + i, 50);
    }
  }
}

void MyMainFrame::DoBiasOFF()
{
  fStatusBar->AddLine("\n");
  std::time_t result = std::time(nullptr);
  fStatusBar->AddLine(std::asctime(std::localtime(&result)));
  fStatusBar->AddLine("Setting Bias OFF");
  fStatusBar->ShowBottom();

  uint8_t serial;

  for (int i = 0; i < 16; i++)
  {
    if (A7585_GetSerialNumber(iic_rasp, I2C_ADD + i, serial))
    {
      A7585_Set_Enable(iic_rasp, I2C_ADD + i, false);
      A7585_Set_V(iic_rasp, I2C_ADD + i, 0);
    }
  }
}

void MyMainFrame::DoBiasUPall()
{
  float v;

  fStatusBar->AddLine("\n");
  std::time_t result = std::time(nullptr);
  fStatusBar->AddLine(std::asctime(std::localtime(&result)));
  fStatusBar->AddLine("Increasing bias by 5V");
  fStatusBar->ShowBottom();

  for (int i = 0; i < 16; i++)
  {
    if (A7585_GetSerialNumber(iic_rasp, I2C_ADD + i, v))
    {
      A7585_Set_V(iic_rasp, I2C_ADD + i, A7585_GetVout(iic_rasp, I2C_ADD + 1, v) + 5);
    }
  }
}

void MyMainFrame::DoBiasDWNall()
{
  float v;

  fStatusBar->AddLine("\n");
  std::time_t result = std::time(nullptr);
  fStatusBar->AddLine(std::asctime(std::localtime(&result)));
  fStatusBar->AddLine("Decreasing bias by 5V");
  fStatusBar->ShowBottom();

  for (int i = 0; i < 16; i++)
  {
    if (A7585_GetSerialNumber(iic_rasp, I2C_ADD + i, v))
    {
      A7585_Set_V(iic_rasp, I2C_ADD + i, A7585_GetVout(iic_rasp, I2C_ADD + 1, v) - 5);
    }
  }
}

void MyMainFrame::DoSetBias()
{
  float serial;
  TString id = fID->GetNumber();
  TString volt = fVOLT->GetNumber();

  if (A7585_GetSerialNumber(iic_rasp, id.Atoi(), serial))
  {
    A7585_Set_V(iic_rasp, id.Atoi(), volt.Atoi());

    fStatusBar->AddLine("\n");
    std::time_t result = std::time(nullptr);
    fStatusBar->AddLine(std::asctime(std::localtime(&result)));
    fStatusBar->AddLine("Setting device ID " + id + " to " + volt + "V");
    fStatusBar->ShowBottom();
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

int biascontrol()
{
  // Popup the GUI...
  new MyMainFrame(gClient->GetRoot(), 1000, 200);

  //TODO: add error handling when opening
  iic_rasp = open(I2C_PATH, O_RDWR);
  if (iic_rasp < 0)
  {
    std::cout << "Error opening the I2C interface" << std::endl;
    return -1;
  }

  for (int i = 0; i < 16; i++)
  {
    A7585_Set_Enable(iic_rasp, I2C_ADD + i, true);
    A7585_Set_Mode(iic_rasp, I2C_ADD + i, HVFB_digital);
    A7585_Set_Enable(iic_rasp, I2C_ADD + i, 0);
    A7585_Set_MaxV(iic_rasp, I2C_ADD + i, 80);
    A7585_Set_RampVs(iic_rasp, I2C_ADD + i, 2);
  }

  return 1;
}

int main(int argc, char **argv)
{
  TApplication theApp("App", &argc, argv);

  if (biascontrol())
  {
    theApp.Run();
  }

  return 0;
}
