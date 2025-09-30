#include <iostream>
#include "TFile.h"
#include "TTree.h"
#include "TString.h"
#include "TH1.h"
#include "TGraph.h"
#include "anyoption.h"
#include <ctime>
#include <tuple>
#include <fstream>
#include <sstream>

#include "Logger.h"
#include "PAPERO.h"

#define max_detectors 16
#define ADC_N 10

template <typename T>
void print(std::vector<T> const &v)
{
    for (auto i : v)
    {
        std::cout << std::hex << i << ' ' << std::endl;
    }
    std::cout << '\n';
}

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

template <typename T>
std::vector<T> reorder_DUNE(std::vector<T> const &v)
{
    std::vector<T> reordered_vec(v.size());
    int j = 0;
    constexpr int order[] = {1, 0, 3, 2, 4, 8, 6, 5, 9, 7};
    for (int ch = 0; ch < 192; ch++)
    {
        for (int adc : order)
        {
            reordered_vec.at(adc * 192 + ch) = v.at(j);
            j++;
        }
    }
    return reordered_vec;
}

template <typename T>
std::vector<T> reorder_DAMPE(std::vector<T> const &v)
{
    std::vector<T> reordered_vec(v.size());
    int j = 0;
    constexpr int order[] = {1, 0};
    for (int ch = 0; ch < 192; ch++)
    {
        for (int adc : order)
        {
            reordered_vec.at(adc * 192 + ch) = v.at(j);
            j++;
        }
    }
    return reordered_vec;
}

AnyOption *opt; // Handle the option input

int main(int argc, char *argv[])
{
    opt = new AnyOption();
    opt->addUsage("Usage: ./flat_convert [options]");
    opt->addUsage("");
    opt->addUsage("Options: ");
    opt->addUsage("  -h, --help       ................................. Print this help ");
    opt->addUsage("  -v, --verbose    ................................. Verbose ");
    opt->addUsage("  --boards         ................................. Number of DE10Nano boards connected ");
    opt->addUsage("  --nevents        ................................. Number of events to be read ");
    opt->addUsage("  --cal-file       ................................. Calibration file for baseline subtraction");
    opt->addUsage("  --input          ................................. Input binary data file");
    opt->addUsage("  --output         ................................. Output ROOT file");
    opt->addUsage("  --gsi            ................................. To convert data from GSI hybrids (10 ADC per detector)");
    opt->addUsage("  --dune           ................................. To convert data from protoDUNE setup (3 DAMPE detectors with adapter)");
    opt->setFlag("help", 'h');
    opt->setFlag("verbose", 'v');
    opt->setOption("boards");
    opt->setOption("nevents");
    opt->setOption("cal-file");
    opt->setOption("input");
    opt->setOption("output");
    opt->setFlag("gsi");
    opt->setFlag("dune");

    opt->processFile("./options.txt");
    opt->processCommandArgs(argc, argv);

    TFile *foutput;

    if (!opt->hasOptions())
    { /* print usage if no options */
        opt->printUsage();
        delete opt;
        return 2;
    }

    bool verbose = false;
    if (opt->getFlag("verbose") || opt->getFlag('v'))
        verbose = true;

    // Open binary data file
    std::fstream file(opt->getArgv(0), std::ios::in | std::ios::out | std::ios::binary);
    if (file.fail())
    {
        std::cout << "ERROR: can't open input file" << std::endl; // file could not be opened
        return 2;
    }

    std::cout << " " << std::endl;
    std::cout << "Processing file " << opt->getArgv(0) << std::endl;

    // Create output ROOT file
    TString output_filename = opt->getArgv(1);
    foutput = new TFile(output_filename.Data(), "RECREATE", "Flat converted data");
    foutput->cd();
    foutput->SetCompressionLevel(3);
#if ROOT_VERSION_CODE >= ROOT_VERSION(6,32,0)
    foutput->SetCompressionAlgorithm(ROOT::RCompressionSetting::EAlgorithm::kZLIB);
#else
    foutput->SetCompressionAlgorithm(ROOT::kZLIB);
#endif

    // Load calibration if provided
    std::vector<std::vector<float>> pedestals(max_detectors, std::vector<float>(384, 0.0f));
    std::vector<std::vector<float>> sigmas(max_detectors, std::vector<float>(384, 1.0f));
    bool useCalibration = false;
    if (opt->getValue("cal-file")) {
        std::string calFile = opt->getValue("cal-file");
        std::cout << "\tLoading calibration from: " << calFile << std::endl;
        std::ifstream cal(calFile);
        if (cal.is_open()) {
            std::string line;
            while (std::getline(cal, line)) {
                if (line.empty() || line[0] == '#') continue;
                // Parse comma or space separated calibration line
                for (char &c : line) if (c == ',' || c == '\t') c = ' ';
                std::istringstream iss(line);
                std::vector<float> values;
                float val;
                while (iss >> val) values.push_back(val);
                if (values.size() >= 6) {
                    int channel = static_cast<int>(values[0]);  // Physical channel number
                    int chip = static_cast<int>(values[1]);     // Chip number (detector)
                    float pedestal = values[3];
                    float sigma_value = values[5];
                    
                    if (chip < max_detectors && channel >= 0 && channel < 384) {
                        pedestals[chip][channel] = pedestal;
                        sigmas[chip][channel] = sigma_value;
                    }
                }
            }
            cal.close();
            useCalibration = true;
            std::cout << "\tCalibration loaded successfully" << std::endl;
        } else {
            std::cout << "\tWarning: Could not open calibration file, proceeding without baseline subtraction" << std::endl;
        }
    }

    // Initialize TTree(s) for baseline-subtracted data
    std::vector<unsigned int> raw_event_buffer;
    std::vector<TTree *> detectorTrees(4);
    std::vector<std::vector<float>*> detectorData(4);
    for (int d = 0; d < 4; ++d) {
        detectorTrees[d] = new TTree(Form("detector%d", d), Form("Baseline-subtracted data for detector %d", d));
        detectorData[d] = new std::vector<float>();
        detectorTrees[d]->Branch("data", &detectorData[d]);
    }

    // Trees for raw (unsubtracted) data
    std::vector<TTree *> rawDetectorTrees(4);
    std::vector<std::vector<float>*> rawDetectorData(4);
    for (int d = 0; d < 4; ++d) {
        rawDetectorTrees[d] = new TTree(Form("raw_detector%d", d), Form("Raw data for detector %d", d));
        rawDetectorData[d] = new std::vector<float>();
        rawDetectorTrees[d]->Branch("raw_data", &rawDetectorData[d]);
    }

    // Trees for sigmas (static, filled once)
    std::vector<TTree *> sigmaTrees(4);
    std::vector<std::vector<float>*> sigmaData(4);
    for (int d = 0; d < 4; ++d) {
        sigmaTrees[d] = new TTree(Form("sigma%d", d), Form("Sigma values for detector %d", d));
        sigmaData[d] = new std::vector<float>();
        sigmaTrees[d]->Branch("sigma", &sigmaData[d]);
        *sigmaData[d] = sigmas[d];
        sigmaTrees[d]->Fill();
    }

    // Trees for pedestals (static, filled once)
    std::vector<TTree *> pedestalTrees(4);
    std::vector<std::vector<float>*> pedestalData(4);
    for (int d = 0; d < 4; ++d) {
        pedestalTrees[d] = new TTree(Form("pedestal%d", d), Form("Pedestal values for detector %d", d));
        pedestalData[d] = new std::vector<float>();
        pedestalTrees[d]->Branch("pedestal", &pedestalData[d]);
        *pedestalData[d] = pedestals[d];
        pedestalTrees[d]->Fill();
    }

    // Per-event info tree (one entry per complete event across all boards)
    TTree *event_info = new TTree("event_info", "event_info");
    Long64_t out_evt_size = 0;
    Long64_t out_fw_version = 0;
    Long64_t out_trigger_number = 0;
    Long64_t out_board_id = 0;
    Long64_t out_timestamp = 0;
    Long64_t out_ext_timestamp = 0;
    Long64_t out_trigger_id = 0;
    Long64_t out_file_offset = 0;
    Int_t    out_event_index = 0;
    event_info->Branch("event_index",     &out_event_index,   "event_index/I");
    event_info->Branch("evt_size",        &out_evt_size,      "evt_size/L");
    event_info->Branch("fw_version",      &out_fw_version,    "fw_version/L");
    event_info->Branch("trigger_number",  &out_trigger_number,"trigger_number/L");
    event_info->Branch("board_id",        &out_board_id,      "board_id/L");
    event_info->Branch("timestamp",       &out_timestamp,     "timestamp/L");
    event_info->Branch("ext_timestamp",   &out_ext_timestamp, "ext_timestamp/L");
    event_info->Branch("trigger_id",      &out_trigger_id,    "trigger_id/L");
    event_info->Branch("file_offset",     &out_file_offset,   "file_offset/L");

    bool dune = false;
    if (opt->getValue("dune"))
    {
        dune = true;
        std::cout << "\tFormatting data for protoDUNE setup" << std::endl;
    }

    // Find if there is an offset before first event
    uint64_t offset = 0;
    offset = seek_first_evt_header(file, offset, verbose);
    uint64_t padding_offset = 0;

    // Read raw events and write to TTree
    bool is_good = false;
    int evtnum = 0;
    int evt_to_read = -1;
    int boards = 0;
    bool gsi = false;
    unsigned long fw_version = 0;
    int board_id = -1;
    int trigger_number = -1;
    int trigger_id = -1;
    int evt_size = 0;
    unsigned long timestamp = 0;
    unsigned long ext_timestamp = 0;
    int boards_read = 0;
    float mean_rate = 0;
    std::tuple<bool, unsigned long, unsigned long, unsigned long, unsigned long, unsigned long, unsigned long, unsigned long, uint64_t> evt_retValues;

    // Aggregate per-event external timestamp across boards: prefer first non-zero
    Long64_t ev_ext_timestamp = 0; bool have_ev_ext = false;

    uint64_t old_offset = 0;
    char dummy[100];

    if (dune)
    {
        boards = 1;
    }
    else if (!opt->getValue("boards"))
    {
        std::cout << "ERROR: you need to provide the number of boards connected" << std::endl;
        return 2;
    }
    else
    {
        boards = atoi(opt->getValue("boards"));
    }

    if (opt->getValue("gsi"))
    {
        gsi = true;
        std::cout << "\tFormatting data for GSI hybrids" << std::endl;
    }

    if (opt->getValue("nevents"))
    {
        evt_to_read = atoi(opt->getValue("nevents"));
    }

    while (!file.eof())
    {
        is_good = false;
        if (evt_to_read > 0 && evtnum == evt_to_read) // stop reading after the number of events specified
            break;

        if (boards_read == 0) {
            // New event starting: reset event-level aggregates
            ev_ext_timestamp = 0; have_ev_ext = false;
            if( !read_evt_header(file, offset, verbose) ) // check for event header if this is the first board
                break;
        }

        evt_retValues = read_de10_header(file, offset, verbose); // read de10 header
        is_good = std::get<0>(evt_retValues);

        if (is_good)
        {
            boards_read++;
            evt_size = std::get<1>(evt_retValues);
            fw_version = std::get<2>(evt_retValues);
            trigger_number = std::get<3>(evt_retValues);
            board_id = std::get<4>(evt_retValues);
            timestamp = std::get<5>(evt_retValues);
            ext_timestamp = std::get<6>(evt_retValues);
            trigger_id = std::get<7>(evt_retValues);
            offset = std::get<8>(evt_retValues);

            if (trigger_number%100 == 0) std::cout << "\r\tReading event " << evtnum << std::flush;

            if (verbose)
            {
                std::cout << "\tBoard ID " << board_id << std::endl;
                std::cout << "\tBoards read " << boards_read << " out of " << boards << std::endl;
                std::cout << "\tTrigger ID " << trigger_id << std::endl;
                std::cout << "\tFW version is: " << std::hex << fw_version << std::dec << std::endl;
                std::cout << "\tEvt lenght: " << evt_size << std::endl;
            }

            if (fw_version == 0xffffffff9fd68b40)
            {
                padding_offset = 1024;
                board_id = board_id - 300;
                raw_event_buffer.clear();
                raw_event_buffer = reorder_DAMPE(read_event(file, offset, evt_size, verbose, false));
            }
            else
            {
                padding_offset = 0;
                raw_event_buffer.clear();
                if (!dune)
                {
                    raw_event_buffer = reorder(read_event(file, offset, evt_size, verbose, false));
                }
                else
                {
                    raw_event_buffer = reorder_DUNE(read_event(file, offset, evt_size, verbose, false));
                }
            }

            // Update event-level external timestamp aggregation (prefer first non-zero)
            if (!have_ev_ext && ext_timestamp != 0) {
                ev_ext_timestamp = static_cast<Long64_t>(ext_timestamp);
                have_ev_ext = true;
            }

            if (!gsi && !dune)
            {
                // Process two detectors per board
                for (int detIdx = 2 * board_id; detIdx < 2 * board_id + 2; ++detIdx) {
                    if (detIdx >= 4) continue;
                    detectorData[detIdx]->clear();
                    rawDetectorData[detIdx]->clear();
                    int start = (detIdx % 2 == 0) ? 0 : raw_event_buffer.size() / 2;
                    int end = (detIdx % 2 == 0) ? raw_event_buffer.size() / 2 : raw_event_buffer.size();
                    for (int i = start; i < end && i < 384; ++i) {
                        float raw = static_cast<float>(raw_event_buffer[i]);
                        float subtracted = useCalibration ? raw - pedestals[detIdx][i] : raw;
                        detectorData[detIdx]->push_back(subtracted);
                        rawDetectorData[detIdx]->push_back(raw);
                    }
                }
            }
            else if (gsi)
            {
                // Remove holes as in PAPERO_convert
                for (int hole = 1; hole <= 10; hole++)
                {
                    raw_event_buffer.erase(raw_event_buffer.begin() + hole * 64, raw_event_buffer.begin() + (hole + 1) * 64);
                }
                int detIdx = 2 * board_id;
                if (detIdx < 4) {
                    detectorData[detIdx]->clear();
                    rawDetectorData[detIdx]->clear();
                    for (size_t i = 0; i < raw_event_buffer.size(); ++i) {
                        float raw = static_cast<float>(raw_event_buffer[i]);
                        float subtracted = useCalibration ? raw - pedestals[detIdx][i] : raw;
                        detectorData[detIdx]->push_back(subtracted);
                        rawDetectorData[detIdx]->push_back(raw);
                    }
                }
            }
            else if (dune)
            {
                uint adc_length = raw_event_buffer.size() / ADC_N;
                for (int detIdx = 2 * board_id; detIdx < 2 * board_id + 4; ++detIdx) {
                    if (detIdx >= 4) continue;
                    detectorData[detIdx]->clear();
                    rawDetectorData[detIdx]->clear();
                    
                    // Each detector gets 2*adc_length channels as in PAPERO_convert
                    int start_offset = (detIdx - 2 * board_id) * 2 * adc_length;
                    int end_offset = start_offset + 2 * adc_length;
                    
                    for (int i = start_offset; i < end_offset && i < raw_event_buffer.size(); ++i) {
                        float raw = static_cast<float>(raw_event_buffer[i]);
                        float subtracted = useCalibration ? raw - pedestals[detIdx][i - start_offset] : raw;
                        detectorData[detIdx]->push_back(subtracted);
                        rawDetectorData[detIdx]->push_back(raw);
                    }
                }
            }

            if (boards_read == boards)
            {
                // Fill detector trees
                for (int d = 0; d < 4; ++d) {
                    detectorTrees[d]->Fill();
                    rawDetectorTrees[d]->Fill();
                }
                // Record event info
                out_event_index     = evtnum;
                out_evt_size        = static_cast<Long64_t>(evt_size);
                out_fw_version      = static_cast<Long64_t>(fw_version);
                out_trigger_number  = static_cast<Long64_t>(trigger_number);
                out_board_id        = static_cast<Long64_t>(board_id);
                out_timestamp       = static_cast<Long64_t>(timestamp);
                out_ext_timestamp   = have_ev_ext ? ev_ext_timestamp : static_cast<Long64_t>(ext_timestamp);
                out_trigger_id      = static_cast<Long64_t>(trigger_id);
                out_file_offset     = static_cast<Long64_t>(offset);
                event_info->Fill();
                boards_read = 0;
                evtnum++;
                offset = (uint64_t)file.tellg() + padding_offset + 8;
            }
            else
            {
                offset = (uint64_t)file.tellg() + padding_offset + 4;
                if (verbose)
                {
                    std::cout << "WARNING: not all boards were read" << std::endl;
                }
            }
        }
        else
        {
            break;
        }
    }

    std::cout << "\n\tClosing file after " << evtnum << " events" << std::endl;

    // Write trees
    for (int d = 0; d < 4; ++d) {
        detectorTrees[d]->Write();
        rawDetectorTrees[d]->Write();
        sigmaTrees[d]->Write();
        pedestalTrees[d]->Write();
    }
    event_info->Write();

    foutput->Close();
    file.close();
    return 0;
}
