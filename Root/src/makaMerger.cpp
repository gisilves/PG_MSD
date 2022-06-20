/*!
  @file makaMerger.cpp
  @brief Merger to collect data from remote detectors
  @author Mattia Barbanera (mattia.barbanera@infn.it)
*/

#include "makaMerger.h"
#include "utility.h"

makaMerger::makaMerger(int port, int verb, bool _net):tcpServer(port, verb){
  //Initialize parameters
  kNEvts = 0;
  kCmdLen = 24;
  runStop();
  clearDetLists();

  //On-line monitor with UDP server
  omClient = new udpClient(kUdpAddr, kUdpPort);

  //Initialize server
  if (_net){
    kAddr.sin_family      = AF_INET; //For network communication
  } else {
    kAddr.sin_family      = AF_UNIX; //For in-system communication
  }
  kBlocking = true;
  Setup();

}

makaMerger::~makaMerger(){
  StopListening();
  runStop();
  clearDetLists();
}

/*------------------------------------------------------------------------------
  Detector list and set-up
------------------------------------------------------------------------------*/
void makaMerger::clearDetLists(){
  kDetAddrs.clear();
  kDetPorts.clear();
}

void makaMerger::addDet(char* _addr, int _port){
  kDetAddrs.push_back(_addr);
  kDetPorts.push_back(_port);
}

void makaMerger::clearDetectors(){
  for (uint32_t ii=0; ii<kDet.size(); ii++) {
    if (kDet[ii]) delete kDet[ii];
  }
  kDet.clear();
}

void makaMerger::setUpDetectors(){
  for (uint32_t ii=0; ii<kDetAddrs.size(); ii++) {
    kDet.push_back(new tcpclient(kDetAddrs[ii], kDetPorts[ii], kVerbosity));
  }
}
//------------------------------------------------------------------------------

/*------------------------------------------------------------------------------
  Run managment
------------------------------------------------------------------------------*/
void makaMerger::runStart(){
  kNEvts   = 0;
  kRunning = true;

  printf("%s) Setup Detectors\n", __METHOD_NAME__);
  //Start clients
  setUpDetectors();

  printf("%s) Spawn thread\n", __METHOD_NAME__);
  //Start thread to merge data
  kMerger3d = std::thread(&makaMerger::merger, this);
}

void makaMerger::runStop(int _sleep){
  sleep(_sleep);
  
  printf("%s) Stop run\n", __METHOD_NAME__);
  kRunning = false;

  printf("%s) Clear detectors\n", __METHOD_NAME__);
  //Close clients
  clearDetectors();

  printf("%s) Stop thread\n", __METHOD_NAME__);
  //Stop thread
  if (kMerger3d.joinable()) kMerger3d.join();
}
//------------------------------------------------------------------------------

/*------------------------------------------------------------------------------
  Merger, collector
------------------------------------------------------------------------------*/
int makaMerger::fileHeader(FILE* _dataFile){
  //FIXME add values to the beginning of file
  return 0;
}

auto fileFormatTime = [](unsigned int val, size_t ndigits) {
    std::string sval = std::to_string(val);
    if (sval.length() < ndigits) {
        sval = std::string(ndigits - sval.length(), '0').append(sval);
    }
    return sval;
};

auto fileFormatDate = [](uint32_t timel){
  // Construct human-readable date
  time_t time{timel};
  auto humanTime = *gmtime(&time);

  std::string dateTime;
  dateTime.append(std::to_string(humanTime.tm_year + 1900));
  dateTime.append(fileFormatTime(humanTime.tm_mon + 1, 2));
  dateTime.append(fileFormatTime(humanTime.tm_mday, 2));
  dateTime.append("_");
  dateTime.append(fileFormatTime(humanTime.tm_hour, 2));
  dateTime.append(fileFormatTime(humanTime.tm_min, 2));
  dateTime.append(fileFormatTime(humanTime.tm_sec, 2));

  return dateTime;
};

int makaMerger::merger(){
  char dataFileName[255];
  unsigned int lastNEvents = 0;
  using clock_type = std::chrono::system_clock;
  //using clock_type = std::chrono::high_resolution_clock; //Increase precision
  
  //Open a file in the kdataPath folder and name it with UTC
  
  // // copy kRunType and make it all UPPERCASE
  // std::string kRunType_upper{kRunType};
  // std::transform(begin(kRunType_upper), end(kRunType_upper),
  //                   begin(kRunType_upper), std::toupper);

  string humanDate = fileFormatDate(kRunTime);
  sprintf(dataFileName,"%s/SCD_RUN%05d_%s_%s.dat", kDataPath.data(), kRunNum, kRunType, humanDate.c_str());

  printf("%s) Opening output file: %s\n", __METHOD_NAME__, dataFileName);
  FILE* dataFileD = fopen(dataFileName,"w");
  if (dataFileD == nullptr) {
    printf("%s) Error: file %s could not be created. Do the data dir %s exist?\n", __METHOD_NAME__, dataFileName, kDataPath.data());
    return -1;
  }

  //----------------------------------------------------------------------------
  //Collect data from clients
  while(kRunning) {
    usleep(100); //?
    auto start = clock_type::now();
    collector(dataFileD);
    auto stop = clock_type::now();

    //if(kNEvts != lastNEvents){
    if(kNEvts%10 == 0){
      std::cout << "\rEvent " << kNEvts << " last recordEvents took " << std::chrono::duration_cast<std::chrono::microseconds>(stop - start).count() << " us                            " << std::flush;
      lastNEvents = kNEvts;
    }
  }
  std::cout << std::endl;
  
  //----------------------------------------------------------------------------
  //Close file
  fclose(dataFileD);
  printf("%s) File %s closed\n", __METHOD_NAME__, dataFileName);

  return 0;

}

int makaMerger::collector(FILE* _dataFile){
  int readRet = 0;
  int writeRet = 0;
  //std::vector<uint32_t*> evts(det.size(), "");
  uint32_t evtLen = 0;
  uint32_t evtLen_tot = 0;
  std::vector<uint32_t> evt(652);

  // FIX ME: at most 64 detectors
  std::bitset<64> replied{0};
  
  constexpr uint32_t header = 0xfa4af1ca;//FIX ME: this header must be done properly. In particular the real length (written by this master, not the one in the payload, after the SoP word) 
  bool headerWritten = false;
  // FIX ME: replace kRunning with proper timeout
  do {
    for (uint32_t ii=0; ii<kDet.size(); ii++) {
      if(!replied[ii]){
        uint32_t readSingle = (getEvent(evt, evtLen, ii));
        readRet += readSingle;
        if(evtLen){
          replied[ii] = true;
        }
        evtLen_tot += evtLen;

	      // only write the header when the first board replies
	      if(replied.count() == 1 && !headerWritten){
	        ++kNEvts;
	        fwrite(&header, 4, 1, _dataFile); //header to file
          omClient->Tx(&header, 4); //header to OM
	        headerWritten = true;
	      }
	      writeRet += fwrite(evt.data(), evtLen, 1, _dataFile); //Event to file
        //omClient->Tx(evt.data(), evtLen); //Event to OM

	      if (kVerbosity>0) {
	        printf("%s) Get event from DE10 %s\n", __METHOD_NAME__, kDetAddrs[ii]);
	        printf("  Bytes read: %d/%d\n", readSingle, evtLen);
	        printf("  Writes performed: %d/%lu\n", writeRet, kDet.size());
	      }
      }
    }
  } while (replied.count() && (replied.count() != kDet.size()) && kRunning);

  //Everything is read and dumped to file
  if (evtLen_tot!=0) {
    if (readRet != (int)evtLen_tot || writeRet != (int)(kDet.size())) {
      printf("%s):\n", __METHOD_NAME__);
      printf("    Bytes read: %d/%u\n", readRet, evtLen_tot);
      printf("    Writes performed: %d/%d\n", writeRet, (int)(kDet.size()));
      return -1;
    }
  }
  else {
    if (kVerbosity>1) {
      printf("%s) Total event lenght was 0\n", __METHOD_NAME__);
    }
  }
  return 0;
}


int makaMerger::getEvent(std::vector<uint32_t>& _evt, uint32_t& _evtLen, int _det){
  //Get the event from HPS and loop here until all data are read
  uint32_t evtRead = 0;
  kDet[_det]->ReceiveInt(_evtLen); //in uint32_t units
  
  if (_evt.size()<_evtLen) _evt.resize(_evtLen);
  _evtLen *= sizeof(uint32_t);  //in byte units
  while (evtRead < _evtLen) {
    evtRead += kDet[_det]->Receive(&_evt[evtRead/sizeof(uint32_t)], _evtLen-evtRead);
  }

  return evtRead;
}
//------------------------------------------------------------------------------


/*------------------------------------------------------------------------------
  makaMerger TCP server
------------------------------------------------------------------------------*/
void* makaMerger::listenCmd(){
  //Accept new connections and handshake commands length
  AcceptConnection();
  cmdLenHandshake();

  bool listenCmd = true;
  int bytesRead  = 0;
  while(listenCmd) {
    char msg[256]="";
    bytesRead = 0;

    //Read the command
    bytesRead = Rx(msg, kCmdLen);

    //Check if the read is ok and process its content
    if(bytesRead < 0) {
      if (EAGAIN==errno || EWOULDBLOCK==errno) {
        printf("%s) errno: %d\n", __METHOD_NAME__, errno);
      }
      else {
        print_error("%s) Read error: (%d)\n", __METHOD_NAME__, errno);
      }
    }
    else if (bytesRead==0) {
      listenCmd = false;
      printf("%s) Client closed the connection\n", __METHOD_NAME__);
    }
    else {
      processCmds(msg);
    }
    bzero(msg, sizeof(msg));
  }

  return nullptr;
}

void makaMerger::cmdLenHandshake(){
  Rx(&kCmdLen, sizeof(int));
  printf("%s) Updating command length to %d\n", __METHOD_NAME__, kCmdLen);
  Tx(&kCmdLen, sizeof(int));
  return;
}

void makaMerger::processCmds(char* msg){
  if (strcmp(msg, "cmd=setup") == 0) {
    cmdReply("setup");

    clearDetLists();

    printf("%s) Received setup command\n", __METHOD_NAME__);

    addDet("192.168.2.104", 5001);
    // addDet("192.168.2.102", 5001);
    // addDet("192.168.2.103", 5001);
    kDataPath = "./data/";

    Tx(&kOkVal, sizeof(kOkVal));
    
    //int pktLen = 0; //Packet length in bytes
    //void* rxData;
    //
    //clearDetLists();
    //
    //printf("%s) Received setup command\n", __METHOD_NAME__);
    //
    //int temp = 0;
    ////Receive length and configuration struct
    //Rx(&pktLen, sizeof(int));
    //printf("Length of next configuration packet: %u\n", pktLen);
    //rxData = malloc(pktLen);
    //temp = Rx(rxData, pktLen);
    //printf("Configurations received bytes: %d\n", temp);
    ////Convert received data into struct
    //sp = (setupPacket*)rxData;
    //printf("Configurations converted\n");
    //
    //printf("Size of: sp: %ld - rxData %ld\n", sizeof(sp), sizeof(rxData));
    //printf("Size of: detNum: %ld - pathLen %ld - pktLen %ld\n", sizeof(sp->detNum), sizeof(sp->pathLen), sizeof(sp->pktLen));
    //
    //printf("Struct pktLen, pathLen, and detNum: %d, %d, %d\n", sp->pktLen, sp->pathLen, sp->detNum);
    //printf("Path:     %s\n", sp->path.c_str());
    //for (uint32_t ii=0; ii<sp->ports.size(); ii++){
    //  printf("  Detector Address %u:  %s\n", ii, sp->addr[ii]);
    //  printf("  Detector Port %u:     %u\n", ii, sp->ports[ii]);
    //}
    //
    //kDataPath = sp->path;
    //kDetPorts = sp->ports;
    //kDetAddrs = sp->addr;
    //
    //printf("Configurations received:\n");
    //printf("File: %s\n", kDataPath.c_str());
    //printf("%u Detector(s): \n", sp->detNum);
    //for (int i=0; i<sp->detNum; i++){
    //  printf("\t%u: Address: %s - Port: %u\n", i, kDetAddrs[i], kDetPorts[i]);
    //  printf("\t%u: Port: %u\n", i, kDetPorts[i]);
    //}
    //
    //Tx(&kOkVal, sizeof(kOkVal));
    //free(rxData);
    //printf("%d Finished sending\n", __LINE__);

  }
  else if (strcmp(msg, "cmd=runStart") == 0) {
    cmdReply("runStart");

    printf("%s) Received runStart command\n", __METHOD_NAME__);
    
    //Receive run type, number, and time
    char buff[25];
    Rx(&buff, sizeof(char)*25);

    printf("%s) Received from start: |%s|\n", __METHOD_NAME__, buff);

    char typeRx[9];
    memcpy(typeRx, buff, 8);
    //typeRx[9] = 0;
    if (strcmp(typeRx, "BEAM    ") == 0) {
      strcpy(kRunType, "BEAM\0\0\0\0\0");
    } else if (strcmp(typeRx, "CAL     ") == 0) {
      strcpy(kRunType, "CAL\0\0\0\0\0\0");
    } else {
      exit(1);
    }

    //kRunType = (char*)string(buff).substr(0, 8).c_str();
    kRunNum  = strtoul(string(buff).substr(8, 8).c_str(), nullptr, 16);
    kRunTime = strtoul(string(buff).substr(16, 8).c_str(), nullptr, 16);

    //Start run
    printf("%s) Starting run %u: %s - %x ...\n", __METHOD_NAME__, kRunNum,
              kRunType, kRunTime);
    runStart();

    Tx(&kOkVal, sizeof(kOkVal));
  }
  else if (strcmp(msg, "cmd=runStop") == 0) {
    cmdReply("runStop");

    printf("%s) Stop run %u with %u events.\n", __METHOD_NAME__, kRunNum, kNEvts);
    runStop();
    Tx(&kOkVal, sizeof(kOkVal));
  }
  else {
    printf("%s) Unknown message: %s\n", __METHOD_NAME__, msg);
    Tx(&kBadVal, sizeof(kBadVal));
  }
}

void makaMerger::cmdReply(const char* cmd){
  char cmdReadBack[256]="";
  sprintf(cmdReadBack, "rcv=%s", cmd);
  Tx(cmdReadBack, kCmdLen);
}

//------------------------------------------------------------------------------