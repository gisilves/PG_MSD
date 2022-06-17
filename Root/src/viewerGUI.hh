#include <TQObject.h>
#include <RQ_OBJECT.h>
#include "TGraph.h"
#include "makaMerger.h"

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

class MyMainFrame
{
  RQ_OBJECT("MyMainFrame")
private:
  TGMainFrame *fMain;
  TRootEmbeddedCanvas *fEcanvas;
  TGNumberEntry *fNumber, *fNumber1, *fNumber2, *fNumber3;
  TGHorizontalFrame *fHor0, *fHor0b, *fHor0c, *fHor0d, *fHor0e, *fHor1, *fHor3, *fHor4;
  TGVerticalFrame *fVer0, *fVer1;
  TGTextButton *fExit, *fExit2, *fDraw, *fOpen, *fSave, *fOpenCalib, *fStart, *fStop;
  TGLabel *evtLabel, *fileLabel, *pedLabel, *detectorLabel, *detectorLabel2, *boardsLabel;
  TGLabel *calibLabel;
  TGTextView *fStatusBar, *fStatusBar2;
  TGCheckButton *fPed;
  TGraph *gr_event = new TGraph();
  bool newDAQ = false;
  int boards = 1;
  bool udp_run = false;

  // UDP server to on-line monitor
  std::string kUdpAddr = "localhost"; //!< UDP Server address (x.x.x.x format)
  int kUdpPort = 8890;                //!< UDP server port
  udpServer *omServer;

public:
  MyMainFrame(const TGWindow *p, UInt_t w, UInt_t h);
  virtual ~MyMainFrame();
  void DoDraw();
  void DoDrawOM(int evtnum, int detector, char calibfile[200], std::vector<uint32_t> evt);
  void DoOpen();
  void DoClose();
  void DoStart();
  void DoStop();
  void DoLoop();
  void DoOpenCalib(bool newDAQ, int boards);
  void DoOpenCalibOnly();
  void PrintCode(Int_t code);
  void viewer(int evt, int detector, char filename[200], char calibfile[200], int boards);
  ClassDef(MyMainFrame, 0)
};
