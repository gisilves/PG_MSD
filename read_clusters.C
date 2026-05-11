#include <TFile.h>
#include <TTree.h>
#include <vector>
#include <iostream>

#include "cluster.h"


// To compile dictionary:
// 1-  rootcling -f cluster_dict.cxx -c cluster.h src/LinkDef.h
// 2-  g++ -shared -fPIC -o libcluster.so cluster_dict.cxx     $(root-config --cflags --libs)

int read_clusters() 
{
    gSystem->Load("./libcluster.so");

    TFile* f = TFile::Open("test_laser_hef.root", "READ");
    TTree* t = (TTree*)f->Get("board_0_side_0/t_clusters_board_0_side_0");

    std::vector<cluster>* clusters = nullptr;
    t->SetBranchAddress("clusters", &clusters);

    Long64_t nEntries = t->GetEntries();
    for (Long64_t i = 0; i < nEntries; i++) {
        t->GetEntry(i);
        for (const auto& cl : *clusters) {
            std::cout << "address=" << cl.address
                      << " width="   << cl.width
                      << " over="    << cl.over
                      << " board="   << cl.board
                      << " side="    << cl.side
                      << " nStrips=" << cl.ADC.size()
                      << "\n";
            float seed = *std::max_element(cl.ADC.begin(), cl.ADC.end());
            std::cout << "  seed ADC=" << seed << "\n";
        }
    }

    f->Close();
    return 0;
}