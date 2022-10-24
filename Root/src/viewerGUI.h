#ifndef VIEWER_GUI_HH
#define VIEWER_GUI_HH

#include <TQObject.h>
#include <RQ_OBJECT.h>
#include "TGraph.h"
#include "udpSocket.h"
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
  TGNumberEntry *fNumberEvent, *fNumberDet, *fNumberBoard, *fNumberDetOM;
  TGNumberEntry *fxmin, *fxmax, *fymin, *fymax, *fdivisions;
  TGHorizontalFrame *fHor_Buttons, *fHor_OM_Buttons;
  TGHorizontalFrame *fHor_Numbers, *fHor_Numbers_OM;
  TGHorizontalFrame *fHor_Pedestal, *fHor_Pedestal_OM;
  TGHorizontalFrame *fHor_Status, *fHor_Status_OM;
  TGHorizontalFrame *fHor_Files;
  TGHorizontalFrame *fHor_Settings, *fHor_Settings2, *fHor_Settings3, *fHor_Settings4;
  TGTextButton *fExit, *fExit2, *fDraw, *fOpen, *fSave, *fOpenCalib, *fStart, *fStop;
  TGLabel *evtLabel, *fileLabel, *pedLabel, *detectorLabel, *detectorLabel2, *boardsLabel;
  TGLabel *calibLabel;
  TGLabel *xminLabel, *xmaxLabel, *yminLabel, *ymaxLabel, *divisionsLabel;
  TGTextView *fStatusBar, *fStatusBar2;
  TGCheckButton *fPed, *fPed2;
  TGCheckButton *fShowAll;
  TGCheckButton *fShowGrid;
  TGraph *gr_event;
  bool newDAQ = false;
  bool calib_open = false;
  int boards = 1;

  std::vector<calib> calib_data;

  // UDP server to on-line monitor
  std::string kUdpAddr = "localhost"; //!< UDP Server address (x.x.x.x format)
  int kUdpPort = 8890;                //!< UDP server port
  udpServer *omServer;

  std::thread th1;
  bool running = false;

  std::vector<uint32_t> evt;
  std::vector<uint32_t> evt_buffer;
  std::vector<uint32_t> detJ5;
  std::vector<uint32_t> detJ7;

public:
  MyMainFrame(const TGWindow *p, UInt_t w, UInt_t h);
  virtual ~MyMainFrame();
  void DoDraw();
  void DoDrawOM(int evtnum, int detector, char calibfile[200], std::vector<uint32_t> evt);
  void DoOpen();
  void DoClose();
  void DoStart();
  void DoStop();
  void DoGetUDP();
  void JobThread();
  void DoOpenCalib();
  void DoOpenCalibOnly();
  void viewer(int evt, int detector, char filename[200], char calibfile[200], int boards);
  ClassDef(MyMainFrame, 0)
};

#endif