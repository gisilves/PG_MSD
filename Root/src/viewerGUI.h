#ifndef VIEWER_GUI_HH
#define VIEWER_GUI_HH

#include <TQObject.h>
#include <RQ_OBJECT.h>
#include "TGraph.h"
#include "event.h"
#include <thread>

class TGWindow;
class TGMainFrame;
class TRootEmbeddedCanvas;
class TGNumberEntry;
class TGHorizontalFrame;
class TGVerticalFrame;
class TGTextButton;
class TGLabel;
class TGTextView;
class TGLayout;
class TGFrame;
class TGFileDialog;
class TGCheckButton;
class TGPictureButton;
class TThread;

class MyMainFrame
{
  RQ_OBJECT("MyMainFrame")
private:
  TGMainFrame *fMain;
  TRootEmbeddedCanvas *fEcanvas;
  TGNumberEntry *fNumber, *fNumber1, *fNumber2, *fNumber3;
  TGHorizontalFrame *fHor_Buttons;
  TGHorizontalFrame *fHor_Numbers;
  TGHorizontalFrame *fHor_Pedestal;
  TGHorizontalFrame *fHor_Status;
  TGHorizontalFrame *fHor_Files;
  TGTextButton *fExit, *fExit2, *fDraw, *fOpen, *fSave, *fOpenCalib;
  TGLabel *evtLabel, *fileLabel, *pedLabel, *detectorLabel, *detectorLabel2, *boardsLabel;
  TGLabel *calibLabel;
  TGTextView *fStatusBar, *fStatusBar2;
  TGCheckButton *fPed, *fPed2;
  TGCheckButton *fShowAll;
  TGraph *gr_event;
  bool newDAQ = false;
  bool calib_open = false;
  int boards = 1;

  std::vector<calib> calib_data;

public:
  MyMainFrame(const TGWindow *p, UInt_t w, UInt_t h);
  virtual ~MyMainFrame();
  void DoDraw();
  void DoOpen();
  void DoClose();
  void DoOpenCalib();
  void DoOpenCalibOnly();
  void viewer(int evt, int detector, char filename[200], char calibfile[200], int boards);
  ClassDef(MyMainFrame, 0)
};

#endif