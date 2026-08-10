#include <iostream>
#include <thread>
#include <vector>
#include <atomic>
#include <algorithm>
#include "TFile.h"
#include "TTree.h"
#include "TString.h"
#include "TH1.h"
#include "TGraph.h"
#include <ctime>
#include <tuple>
#include <CLI/CLI.hpp>

#include "PAPERO.h"

#define max_detectors 8
#define BUFFER_SIZE 1048576  // 1MB buffer

// One work item = one board's payload for one trigger.
// Recorded during the (sequential) header-scan pass, consumed during
// the (parallel) decode pass.
struct EventWorkItem
{
    uint32_t offset;   // byte offset of the payload, as passed to read_eventHEF
    int evt_size;       // size in the same units used by read_eventHEF
    int det_idx;         // index into raw_events_tree / raw_event_vector
    int global_evtnum;  // original trigger number, for debugging only
};

int main(int argc, char *argv[])
{
    CLI::App app{"HEF_convert"};

    bool verbose = false;
    bool gsi = false;
    int boards = 0;
    int nevents = -1;
    int nthreads = std::max(1u, std::thread::hardware_concurrency());
    int batch_events = 20000; // bound memory: decode/write in batches of this many triggers
    std::string input_file;
    std::string output_file;

    app.add_flag("-v,--verbose", verbose, "Verbose output");
    app.add_option("--nevents", nevents, "Number of events to be read");
    app.add_option("--threads", nthreads, "Number of decode threads (default: hw concurrency)");
    app.add_option("--batch-events", batch_events, "Triggers per index/decode/write batch (default 20000)");
    app.add_option("raw_data_file", input_file, "Raw data input file")->required();
    app.add_option("output_rootfile", output_file, "Output ROOT file")->required();

    CLI11_PARSE(app, argc, argv);

    TFile *foutput;

    // Main file handle: used only for the cheap sequential header scan.
    std::fstream file(input_file.c_str(), std::ios::in | std::ios::out | std::ios::binary);
    if (file.fail())
    {
        std::cout << "ERROR: can't open input file" << std::endl;
        return 2;
    }

    std::ios::sync_with_stdio(false);
    char file_buffer[BUFFER_SIZE];
    file.rdbuf()->pubsetbuf(file_buffer, BUFFER_SIZE);

    std::cout << " " << std::endl;
    std::cout << "Processing file " << input_file.c_str() << std::endl;

    TString output_filename = output_file.c_str();
    foutput = new TFile(output_filename.Data(), "RECREATE", "PAPERO data");
    foutput->cd();
    foutput->SetCompressionLevel(1);
    foutput->SetCompressionAlgorithm(ROOT::RCompressionSetting::EDefaults::kUseGeneralPurpose);

    std::string alphabet = "ABCDEFGHIJKLMNOPQRSTWXYZ";
    std::vector<TTree *> raw_events_tree(max_detectors);
    std::vector<std::vector<uint32_t>> raw_event_vector(max_detectors);
    for (size_t detector = 0; detector < max_detectors; detector++)
    {
        TString ttree_name = (detector == 0) ? "raw_events" : TString("raw_events_") + alphabet.at(detector);
        raw_events_tree.at(detector) = new TTree(ttree_name, ttree_name);
        raw_events_tree.at(detector)->Branch("RAW Event", &raw_event_vector.at(detector));
        raw_events_tree.at(detector)->SetAutoSave(50000000);
    }

    bool is_good = false;
    int evtnum = 0;
    int evt_to_read = -1;
    int board_id = -1;
    int boards_read = 0;
    uint32_t offset = 0;
    uint32_t old_offset = 0;

    std::map<uint16_t, int> detector_ids_map;
    std::vector<uint16_t> detector_ids;

    std::tuple<bool, uint32_t, uint32_t, uint8_t, uint16_t, uint16_t, std::vector<uint16_t>, uint32_t> file_retValues;
    std::tuple<bool, uint32_t, uint32_t, uint32_t, uint32_t, uint64_t, uint64_t, uint32_t, uint32_t, uint32_t, uint32_t, int> de10_retValues;
    std::tuple<bool, timespec, uint32_t, uint32_t, uint16_t, uint16_t, uint16_t, uint32_t> maka_retValues;

    bool new_format = seek_file_header(file, offset, verbose);
    if (new_format)
    {
        std::cout << "New data format" << std::endl;
        file_retValues = read_file_header(file, offset, verbose);
        is_good = std::get<0>(file_retValues);
        boards = std::get<5>(file_retValues);

        detector_ids = std::get<6>(file_retValues);
        for (size_t i = 0; i < detector_ids.size(); i++)
            detector_ids_map[detector_ids.at(i)] = i;

        old_offset = std::get<7>(file_retValues);
        offset = seek_first_evt_header(file, old_offset, verbose);
        if (offset != old_offset)
            std::cout << "WARNING: first evt header has a " << offset - old_offset << " delta value " << std::endl;
    }
    else
    {
        std::cerr << "ERROR: HEF data can only be of new format type, check file" << std::endl;
        return 2;
    }

    if (nevents > 0)
    {
        evt_to_read = nevents;
        std::cout << "\tReading " << evt_to_read << " events" << std::endl;
    }

    std::cout << "\tUsing " << nthreads << " decode threads, batches of "
               << batch_events << " triggers" << std::endl;

    bool file_done = false;

    while (!file_done)
    {
        // -----------------------------------------------------------
        // Pass 1 (sequential): scan headers for one batch of
        // triggers and record where each board's payload lives. This
        // has to stay single-threaded — each header's offset depends
        // on the previous event's size. No payload decoding here.
        // -----------------------------------------------------------
        std::vector<EventWorkItem> work_items;
        work_items.reserve(batch_events * max_detectors);

        int batch_start_evtnum = evtnum;

        while (!file.eof())
        {
            if (evtnum == evt_to_read)
            {
                file_done = true;
                break;
            }
            if (evtnum - batch_start_evtnum >= batch_events)
                break; // batch full, decode/write it, then resume indexing

            is_good = false;
            maka_retValues = read_evt_header(file, offset, verbose);
            if (!std::get<0>(maka_retValues))
            {
                file_done = true;
                break;
            }

            offset = std::get<7>(maka_retValues);
            for (size_t de10 = 0; de10 < std::get<4>(maka_retValues); de10++)
            {
                de10_retValues = read_de10_header(file, offset, verbose);
                is_good = std::get<0>(de10_retValues);

                if (is_good)
                {
                    boards_read++;
                    int evt_size = std::get<1>(de10_retValues) - 2; // TODO: same -2 fixup as before
                    board_id = std::get<4>(de10_retValues);
                    offset = std::get<11>(de10_retValues);

                    int det_idx = detector_ids_map.at(board_id);
                    work_items.push_back(EventWorkItem{offset, evt_size, det_idx, evtnum});

                    offset += evt_size * 4 + 8 + 44;
                }
            }
            boards_read = 0;
            evtnum++;

            if (evtnum % 1000 == 0)
                std::cout << "\r\tIndexed event " << evtnum << std::flush;
        }

        if (work_items.empty())
            break;

        // -----------------------------------------------------------
        // Pass 2 (parallel): decode this batch's payloads. Split into
        // contiguous chunks so each thread's slice stays in original
        // order — we only need to concatenate chunks back in order.
        // Each thread opens its own read-only file handle.
        // -----------------------------------------------------------
        std::vector<std::vector<uint32_t>> decoded(work_items.size());

        size_t total = work_items.size();
        int active_threads = std::max(1, std::min<int>(nthreads, (int)total));
        size_t chunk_size = (total + active_threads - 1) / active_threads;

        std::vector<std::thread> threads;
        std::atomic<bool> decode_error{false};

        for (int t = 0; t < active_threads; t++)
        {
            size_t begin = t * chunk_size;
            size_t end = std::min(begin + chunk_size, total);
            if (begin >= end)
                continue;

            threads.emplace_back([&, begin, end]()
            {
                std::fstream tfile(input_file.c_str(), std::ios::in | std::ios::binary);
                if (tfile.fail())
                {
                    decode_error = true;
                    return;
                }
                std::vector<char> local_buffer(BUFFER_SIZE);
                tfile.rdbuf()->pubsetbuf(local_buffer.data(), local_buffer.size());

                for (size_t i = begin; i < end; i++)
                {
                    const EventWorkItem &item = work_items[i];
                    uint32_t local_offset = item.offset;
                    decoded[i] = std::move(reorder(read_eventHEF(tfile, local_offset, item.evt_size, false)));
                }
            });
        }

        for (auto &th : threads)
            th.join();

        if (decode_error)
        {
            std::cerr << "\nERROR: a decode thread failed to open the input file" << std::endl;
            return 2;
        }

        // -----------------------------------------------------------
        // Pass 3 (sequential): Fill() trees in original order.
        // TTree::Fill isn't thread-safe, so this stays single-threaded,
        // but it's cheap relative to decoding.
        // -----------------------------------------------------------
        for (size_t i = 0; i < work_items.size(); i++)
        {
            const EventWorkItem &item = work_items[i];
            raw_event_vector.at(item.det_idx) = std::move(decoded[i]);
            raw_events_tree.at(item.det_idx)->Fill();
        }

        std::cout << "\r\tWrote through event " << evtnum << std::flush;
    }

    file.close();

    std::cout << "\n\tClosing file after " << std::dec << evtnum << " events" << std::endl;
    int filled = 0;
    std::vector<int> written_board_ids;

    for (size_t detector = 0; detector < raw_events_tree.size(); detector++)
    {
        if (raw_events_tree.at(detector)->GetEntries())
        {
            if (filled == 0)
            {
                raw_events_tree.at(detector)->SetName("raw_events");
                raw_events_tree.at(detector)->SetTitle("raw_events");
                raw_events_tree.at(detector)->Write();
            }
            else
            {
                std::string name = "raw_events_" + alphabet.substr(filled, 1);
                raw_events_tree.at(detector)->SetName(name.c_str());
                raw_events_tree.at(detector)->SetTitle(name.c_str());
                raw_events_tree.at(detector)->Write();
            }
            int real_id = (detector < detector_ids.size()) ? (int)detector_ids.at(detector) : (int)detector;
            written_board_ids.push_back(real_id);
            filled++;
        }
    }

    TTree board_id_tree("board_ids", "board_ids");
    int bid;
    board_id_tree.Branch("board_id", &bid);
    for (int id : written_board_ids)
    {
        bid = id;
        board_id_tree.Fill();
    }
    board_id_tree.Write();

    foutput->Close();
    return 0;
}