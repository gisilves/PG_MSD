#include <TQObject.h>
#include <RQ_OBJECT.h>
#include "TGraph.h"

class TGWindow;
class TGMainFrame;
class TRootEmbeddedCanvas;
class TGNumberEntry;
class TGTextEntry;
class TGHorizontalFrame;
class TGVerticalFrame;
class TGTextButton;
class TGLabel;
class TGTextView;
class TGLayout;
class TGFrame;
class TGFileDialog;
class TGCheckButton;

class MyMainFrame
{
  RQ_OBJECT("MyMainFrame")
private:
  TGMainFrame *fMain;
  TGNumberEntry *fPORT;
  TGTextEntry *fIP;
  TGHorizontalFrame *fHor0, *fHor0b, *fHor1, *fHor2, *fHor3;
  TGVerticalFrame *fVer0, *fVer1;
  TGTextButton *fExit, *fPing, *fClear, *fStatus, *fBiasON, *fBiasOFF, *fBiasUP, *fBiasDWN;
  TGLabel *portLabel, *ipLabel;
  TGTextView *fStatusBar;

public:
  MyMainFrame(const TGWindow *p, UInt_t w, UInt_t h);
  virtual ~MyMainFrame();
  void DoStatus();
  void DoPing();
  void DoClear();
  void DoBiasON();
  void DoBiasOFF();
  void DoBiasUP();
  void DoBiasDWN();
  void DoClose();
  ClassDef(MyMainFrame, 0)
};
