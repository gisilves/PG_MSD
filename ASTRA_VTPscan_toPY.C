#include <iostream>
#include <vector>
#include <fstream>
#include <string>

#include "TFile.h"
#include "TTree.h"
#include "TString.h"

void ASTRA_VTPscan_toPY(TString input_file, TString evts_vtp_change_file, TString py_file)
{
    // Load VTP step boundaries
    std::vector<Long64_t> boundaries;
    std::ifstream cond_in(evts_vtp_change_file.Data());
    if (!cond_in.is_open())
        return;

    Long64_t idx;
    while (cond_in >> idx)
        boundaries.push_back(idx);
    cond_in.close();

    TFile *f = TFile::Open(input_file);
    if (!f || f->IsZombie())
        return;

    TTree *t = static_cast<TTree *>(f->Get("raw_events"));
    if (!t)
        return;

    std::vector<unsigned int> *J5 = nullptr;
    t->SetBranchAddress("RAW Event J5", &J5);

    const Long64_t n = t->GetEntries();

    std::ofstream out(py_file.Data());
    if (!out.is_open())
        return;

    out << "events = [\n";

    int vtp_step = 0;
    size_t boundary_idx = 0;
    Long64_t next_boundary = (boundaries.empty() ? n : boundaries[0]);

    for (Long64_t i = 0; i < n; i++)
    {
        // Increment vtp_step if current event is exactly the boundary
        if (boundary_idx < boundaries.size() && i == next_boundary - 1)
        {
            vtp_step++;
            boundary_idx++;
            next_boundary = (boundary_idx < boundaries.size()) ? boundaries[boundary_idx] : n;
        }

        t->GetEntry(i);

        out << "    [" << vtp_step << ", [";
        const size_t limit = std::min<size_t>(64, J5->size());
        for (size_t k = 0; k < limit; k++)
        {
            out << (*J5)[k];
            if (k + 1 < limit)
                out << ", ";
        }
        out << "]]";
        if (i + 1 < n)
            out << ",";
        out << "\n";
    }

    out << "]\n";

    out.close();
    f->Close();
}
