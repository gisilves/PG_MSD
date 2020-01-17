#include "Riostream.h"
#include <string>
void plotCalib(char *filename, char *notes) {

   TString dir = gSystem->UnixPathName(gInterpreter->GetCurrentMacroName());
   dir.ReplaceAll("plotCalib.C","");
   dir.ReplaceAll("/./","/");
   cout << filename << endl;
   ifstream in;
   in.open(filename);

   
 //Create graph for pedestals and sigmas
  TGraph *gr_ped=new TGraph();
  TGraph *gr_raw_sigmas=new TGraph();
  gr_ped->SetMarkerStyle(23);
  gr_ped->SetMarkerColor(kBlue);
  gr_ped->SetLineColor(kBlue);
  gr_raw_sigmas->SetMarkerStyle(23);
  gr_raw_sigmas->SetMarkerColor(kBlue);
  gr_raw_sigmas->SetLineColor(kBlue);
  TGraph *gr_sigmas=new TGraph();
  gr_sigmas->SetMarkerStyle(23);
  gr_sigmas->SetMarkerColor(kBlue);
  gr_sigmas->SetLineColor(kBlue);

  Float_t strip, va, vachannel, ADC, rawsigma, sigma, status, boh;

   Int_t nlines = 0;
   int graphpoints=0;
   TFile *f = new TFile("basic.root","RECREATE");
   TNtuple *ntuple = new TNtuple("ntuple","data from ascii file","x:y:z");

   string dummyLine;

   for(int k=0; k<18; k++){
   getline(in, dummyLine);
   }

   int maxped, maxraw, maxsigma = -999;
   while (1) {     
       in >> strip >> va >> vachannel >> ADC >> rawsigma >> sigma >> status >> boh;
       
       if (!in.good()) break;
       
       if(ADC > maxped){maxped=ADC;}
       if(rawsigma > maxraw){maxraw=rawsigma;}
       if(sigma > maxsigma){maxsigma=sigma;}
       
       gr_ped->SetPoint(gr_ped->GetN(),graphpoints, ADC);
       gr_raw_sigmas->SetPoint(gr_ped->GetN(),graphpoints, rawsigma);
       gr_sigmas->SetPoint(gr_ped->GetN(),graphpoints, sigma);

       graphpoints++;
       nlines++;
   }
   printf(" found %d points\n",graphpoints);

   in.close();

   TCanvas *c2 = new TCanvas("c2", "c2", 3840, 2160);
   gStyle->SetOptStat(1111111);
   gStyle->SetOptFit(1111);

   c2->SetTitle(filename);
   c2->Divide(2,2);
   c2->cd(1);
   TH1F *frame = gPad->DrawFrame(0, 0,1024,maxped+100);
   frame->SetTitle("Pedestals; channel; ADC");
   //frame->GetYaxis()->SetTitleOffset(1.55);
   frame->GetXaxis()->SetNdivisions(-16);
   gr_ped->SetMarkerSize(0.5);
   gr_ped->Draw("*");
   TLine *line = new TLine(64,0,64,maxped+100);
   line->SetLineColor(kRed);
   line->Draw();
   TLine *line1 = new TLine(128,0,128,maxped+100);
   line1->SetLineColor(kRed);
   line1->Draw();
   TLine *line2 = new TLine(192,0,192,maxped+100);
   line2->SetLineColor(kRed);
   line2->Draw();
   TLine *line3 = new TLine(256,0,256,maxped+100);
   line3->SetLineColor(kRed);
   line3->Draw();
   TLine *line4 = new TLine(320,0,320,maxped+100);
   line4->SetLineColor(kRed);
   line4->Draw();
   TLine *line5 = new TLine(384,0,384,maxped+100);
   line5->SetLineColor(kRed);
   line5->Draw();
   TLine *line6 = new TLine(448,0,448,maxped+100);
   line6->SetLineColor(kRed);
   line6->Draw();
   TLine *line7 = new TLine(512,0,512,maxped+100);
   line7->SetLineColor(kRed);
   line7->Draw();
   TLine *line8 = new TLine(576,0,576,maxped+100);
   line8->SetLineColor(kRed);
   line8->Draw();
   TLine *line9 = new TLine(640,0,640,maxped+100);
   line9->SetLineColor(kRed);
   line9->Draw();
   TLine *line10 = new TLine(704,0,704,maxped+100);
   line10->SetLineColor(kRed);
   line10->Draw();
   TLine *line11 = new TLine(768,0,768,maxped+100);
   line11->SetLineColor(kRed);
   line11->Draw();
   TLine *line12 = new TLine(832,0,832,maxped+100);
   line12->SetLineColor(kRed);
   line12->Draw();
   TLine *line13 = new TLine(896,0,896,maxped+100);
   line13->SetLineColor(kRed);
   line13->Draw();
   TLine *line14 = new TLine(960,0,960,maxped+100);
   line14->SetLineColor(kRed);
   line14->Draw();
   gr_raw_sigmas->Write("P");
   gr_ped->Write("p");
   
   
   c2->cd(2);
    frame = gPad->DrawFrame(0,0,1024,maxraw+10);
    frame->SetTitle("Raw sigmas; channel; raw sigmas (adc)");
    //frame->GetYaxis()->SetTitleOffset(1.55);
    frame->GetXaxis()->SetNdivisions(-16);
    gr_sigmas->SetMarkerSize(0.5);
    gr_sigmas->Draw("*");
    TLine *line = new TLine(64,0,64,maxraw+10);
    line->SetLineColor(kRed);
    line->Draw();
    TLine *line1 = new TLine(128,0,128,maxraw+10);
    line1->SetLineColor(kRed);
    line1->Draw();
    TLine *line2 = new TLine(192,0,192,maxraw+10);
    line2->SetLineColor(kRed);
    line2->Draw();
    TLine *line3 = new TLine(256,0,256,maxraw+10);
    line3->SetLineColor(kRed);
    line3->Draw();
    TLine *line4 = new TLine(320,0,320,maxraw+10);
    line4->SetLineColor(kRed);
    line4->Draw();
    TLine *line5 = new TLine(384,0,384,maxraw+10);
    line5->SetLineColor(kRed);
    line5->Draw();
    TLine *line6 = new TLine(448,0,448,maxraw+10);
    line6->SetLineColor(kRed);
    line6->Draw();
    TLine *line7 = new TLine(512,0,512,maxraw+10);
    line7->SetLineColor(kRed);
    line7->Draw();
    TLine *line8 = new TLine(576,0,576,maxraw+10);
    line8->SetLineColor(kRed);
    line8->Draw();
    TLine *line9 = new TLine(640,0,640,maxraw+10);
    line9->SetLineColor(kRed);
    line9->Draw();
    TLine *line10 = new TLine(704,0,704,maxraw+10);
    line10->SetLineColor(kRed);
    line10->Draw();
    TLine *line11 = new TLine(768,0,768,maxraw+10);
    line11->SetLineColor(kRed);
    line11->Draw();
    TLine *line12 = new TLine(832,0,832,maxraw+10);
    line12->SetLineColor(kRed);
    line12->Draw();
    TLine *line13 = new TLine(896,0,896,maxraw+10);
    line13->SetLineColor(kRed);
    line13->Draw();
    TLine *line14 = new TLine(960,0,960,maxraw+10);
    line14->SetLineColor(kRed);
    line14->Draw();
    gr_raw_sigmas->Write("P");

    c2->cd(3);
    frame = gPad->DrawFrame(0,0,1024,maxsigma+5);
    frame->SetTitle("Sigmas; channel; sigmas (adc)");
    //frame->GetYaxis()->SetTitleOffset(1.55);
    frame->GetXaxis()->SetNdivisions(-16);
    gr_raw_sigmas->SetMarkerSize(0.5);
    gr_raw_sigmas->Draw("*");
    TLine *line = new TLine(64,0,64,maxsigma+5);
    line->SetLineColor(kRed);
    line->Draw();
    TLine *line1 = new TLine(128,0,128,maxsigma+5);
    line1->SetLineColor(kRed);
    line1->Draw();
    TLine *line2 = new TLine(192,0,192,maxsigma+5);
    line2->SetLineColor(kRed);
    line2->Draw();
    TLine *line3 = new TLine(256,0,256,maxsigma+5);
    line3->SetLineColor(kRed);
    line3->Draw();
    TLine *line4 = new TLine(320,0,320,maxsigma+5);
    line4->SetLineColor(kRed);
    line4->Draw();
    TLine *line5 = new TLine(384,0,384,maxsigma+5);
    line5->SetLineColor(kRed);
    line5->Draw();
    TLine *line6 = new TLine(448,0,448,maxsigma+5);
    line6->SetLineColor(kRed);
    line6->Draw();
    TLine *line7 = new TLine(512,0,512,maxsigma+5);
    line7->SetLineColor(kRed);
    line7->Draw();
    TLine *line8 = new TLine(576,0,576,maxsigma+5);
    line8->SetLineColor(kRed);
    line8->Draw();
    TLine *line9 = new TLine(640,0,640,maxsigma+5);
    line9->SetLineColor(kRed);
    line9->Draw();
    TLine *line10 = new TLine(704,0,704,maxsigma+5);
    line10->SetLineColor(kRed);
    line10->Draw();
    TLine *line11 = new TLine(768,0,768,maxsigma+5);
    line11->SetLineColor(kRed);
    line11->Draw();
    TLine *line12 = new TLine(832,0,832,maxsigma+5);
    line12->SetLineColor(kRed);
    line12->Draw();
    TLine *line13 = new TLine(896,0,896,maxsigma+5);
    line13->SetLineColor(kRed);
    line13->Draw();
    TLine *line14 = new TLine(960,0,960,maxsigma+5);
    line14->SetLineColor(kRed);
    line14->Draw();
    gr_raw_sigmas->Write("P");


    char out[256] = filename;
    char label[256] = filename;
    
    c2->cd();
    TPad *newpad=new TPad("newpad","a transparent pad",0,0,1,1);
    newpad->SetFillStyle(4000);
    newpad->Draw();
    newpad->cd();
    TPaveLabel *title = new TPaveLabel(0.55,0.17,0.95,0.27,strcat(strcat(label,"      "),notes));
    title->SetFillColor(10);
    title->SetTextFont(40);
    title->SetTextSize(0.15);
    //    title->SetTextAngle(90.);
    title->Draw();
    
    
    c2->SaveAs(strcat(out, ".pdf"));
}
